"""图传面板：服务器侧「喂图片发图传」，客户端侧「收图传 + 实时预览」。

要点
  * 编解码都在 C++ 里完成，Python 只负责「读视频 -> 喂帧」和「取帧 -> 显示」
  * 端口 / 视频文件在**停止状态**下可改（运行中禁用）
  * 解码格式可「自动探测」或「手动固定为某一种」，运行中也能切
  * **预览「尽量显示」**：由一个专门的显示线程持续取帧并转换，
    而不是用定时器按固定频率去采样（采样会系统性丢帧，见下面 PreviewLink 的说明）
  * 所有颜色取自 gui.theme，明暗主题都正常

显示流水线（收发两侧一致）
    工作线程                                     GUI 线程
    ─────────                                   ─────────
    取一帧 -> 降采样 -> 转 QImage  ──槽──>  QPixmap.fromImage + setPixmap
    总是消费并覆盖槽内容 —— 这样 GUI 恢复时拿到的一定是最新帧，
    不会先闪一下停摆前的旧画面（去掉背压的原因见 PreviewLink.wants()）
"""

from __future__ import annotations

import os
import threading
import time

import numpy as np
from PyQt5.QtCore import Qt, QThread, QTimer, pyqtSignal
from PyQt5.QtGui import QImage, QPixmap
from PyQt5.QtWidgets import (QCheckBox, QComboBox, QFileDialog, QFormLayout,
                             QGridLayout, QGroupBox, QHBoxLayout, QLabel,
                             QLineEdit, QPushButton, QSpinBox, QVBoxLayout,
                             QWidget)

import rm2027c as rm
from gui import theme

# 预览最大宽度：超过就降采样。1080p 每帧完整转 QImage 是 6MB 级拷贝，
# 预览不需要那个精度，而且降采样后 GUI 线程的 QPixmap 转换也快得多。
PREVIEW_MAX_W = 720

# 显示线程没有新帧时的轮询间隔。1ms 保证解码端一出帧就能被拿到，
# 不会因为轮询太慢而漏帧。
POLL_IDLE_SEC = 0.001

# 等待线程退出的上限。正常情况下一轮循环只有毫秒级，远到不了这个上限；
# 到了说明真的卡住了，此时**不能强杀**（见 stop() 里的说明）。
THREAD_JOIN_MS = 3000


def _disconnect_all(worker) -> None:
    """断开 worker 的全部信号。线程可能还要过一会儿才真正退出。"""
    for sig in ("statsReady", "frameAvailable", "failed", "finishedRun"):
        s = getattr(worker, sig, None)
        if s is None:
            continue
        try:
            s.disconnect()
        except TypeError:
            pass        # 没有连接过


def _thread_stuck(label, what: str) -> None:
    """线程没能按时退出时的统一处置：只报错，不强杀、不释放共享资源。"""
    msg = "%s线程未能在 %dms 内退出，已保留资源（请关闭窗口重开）" % (what, THREAD_JOIN_MS)
    print("[video-panel] " + msg, flush=True)
    if label is not None:
        label.setText(msg)


def bgr_to_qimage(img: np.ndarray, max_w: int = PREVIEW_MAX_W) -> tuple:
    """BGR24 numpy -> QImage（已按需整数降采样）。

    用**整数抽取**（img[::k, ::k]）而不是插值缩放：抽取几乎不花时间，
    预览画质足够；插值在 1080p 下反而是可观的开销。

    QImage 可以在非 GUI 线程创建（QPixmap 不行），所以这一步放在显示线程里做。

    返回 (QImage, 显示宽, 显示高)；QImage 已自带数据副本。
    """
    h, w = img.shape[:2]
    if w > max_w:
        step = int(np.ceil(w / max_w))
        img = img[::step, ::step]
    if not img.flags["C_CONTIGUOUS"]:
        img = np.ascontiguousarray(img)
    h, w = img.shape[:2]
    if hasattr(QImage, "Format_BGR888"):
        qi = QImage(img.data, w, h, w * 3, QImage.Format_BGR888).copy()
    else:
        qi = QImage(img.data, w, h, w * 3, QImage.Format_RGB888).rgbSwapped().copy()
    return qi, w, h


class PreviewLink:
    """显示线程 -> GUI 线程 的「最新帧」交接。

    为什么不用定时器采样：
        定时器（比如 16ms）是按**时间**取帧的，只要帧率与定时器周期不是整除关系，
        就会周期性地漏掉或重复帧 —— 这是系统性丢帧。这里改成：显示线程**每解出
        一帧就来取**，GUI 线程能画多快就画多快，于是「尽量显示」。

    为什么不无限排队：
        显示端画不过来时，排队只会让画面越来越滞后（延迟累积）。所以只保留
        最新一帧，旧的直接覆盖 —— 宁可丢旧帧，也要让操作手看到最新的画面。

    通知做了合并：GUI 还没取走上一帧时不再重复通知，避免刷爆事件队列。
    """

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._image = None
        self._meta = None
        self._notify_pending = False
        self.enabled = True          # GUI 线程写，显示线程读

    def wants(self) -> bool:
        """显示线程：现在要不要产出一帧（只看预览是否开着）。

        **曾经这里还要求「槽已空」，那是个错误的优化**：
        GUI 停摆期间槽里会一直留着停摆前的旧帧，因为背压让显示线程既不消费
        C++ 最新帧、也不更新槽 —— 恢复后就会**先闪一下旧画面**（实测先帧 1、
        再帧 33）。现在改为总是消费并覆盖槽内容，「最新帧」才是真的最新。

        代价是被覆盖的那些转换白做了；但降采样后单帧转换开销很小，
        换来的是恢复时立刻显示最新画面。
        """
        return self.enabled

    def put(self, image, meta) -> bool:
        """显示线程：放帧（**覆盖**槽里可能还没被取走的旧帧）。

        返回是否需要通知 GUI。通知做了合并：上一帧还没被取走时不再重复通知，
        所以 GUI 再慢也不会被事件队列刷爆，且取到的始终是最新一帧。
        """
        with self._lock:
            self._image = image
            self._meta = meta
            if self._notify_pending:
                return False
            self._notify_pending = True
            return True

    def take(self):
        """GUI 线程：取走最新帧；没有新帧返回 (None, None)。"""
        with self._lock:
            image, meta = self._image, self._meta
            self._image = None
            self._meta = None
            self._notify_pending = False
            return image, meta

    def clear(self) -> None:
        with self._lock:
            self._image = None
            self._meta = None
            self._notify_pending = False


class PreviewLabel(QLabel):
    """预览控件：只负责把 QImage 画出来（不做任何 numpy 转换）。"""

    def __init__(self, placeholder: str, parent=None):
        super().__init__(placeholder, parent)
        self.setAlignment(Qt.AlignCenter)
        self.setMinimumHeight(200)
        self._pixmap = None
        self._size = (0, 0)
        self.refresh_theme()

    def refresh_theme(self) -> None:
        c = theme.palette_colors()
        self.setStyleSheet(
            "QLabel { background: %s; color: %s; border: 1px solid %s; }"
            % (c["panel"], c["muted"], c["mid"]))

    def set_image(self, qimage: QImage, src_w: int = 0, src_h: int = 0) -> None:
        """显示一帧。转换已经在显示线程做完了，这里只剩 QPixmap 包装。"""
        pm = QPixmap.fromImage(qimage)
        self._pixmap = pm
        self._size = (src_w or qimage.width(), src_h or qimage.height())
        self.setPixmap(pm)

    def clear_image(self, text: str) -> None:
        self._pixmap = None
        self._size = (0, 0)
        self.setText(text)

    @property
    def source_size(self):
        """最近一帧的原始尺寸 (w, h)。"""
        return self._size


def _fmt_preview_caption(label: str, frame_no: int, src_w: int, src_h: int,
                         disp_w: int, disp_h: int, extra: str = "") -> str:
    if src_w and disp_w and src_w != disp_w:
        size = "源 %dx%d → 显示 %dx%d" % (src_w, src_h, disp_w, disp_h)
    else:
        size = "%dx%d" % (src_w, src_h)
    return "%s：第 %d 帧（%s%s）" % (label, frame_no, size, extra)


# ===========================================================================
# 服务器侧：发送图传
# ===========================================================================
class SendWorker(QThread):
    """读视频文件 -> 缩放 -> 按帧率喂给 C++ 编码发送，同时产出发送预览。

    预览不再按固定帧率抽帧：**每发一帧就产出一帧预览**（预览关闭时除外）。
    产出的帧直接覆盖 PreviewLink 里的旧帧，GUI 取到的始终是最新一帧。
    """

    statsReady = pyqtSignal(object)      # rm.VideoSenderStats
    frameAvailable = pyqtSignal()        # 无载荷：GUI 自己去槽里取最新帧
    failed = pyqtSignal(str)
    finishedRun = pyqtSignal()

    def __init__(self, sender, path: str, fps: int, width: int, height: int,
                 loop: bool, link: PreviewLink, parent=None):
        super().__init__(parent)
        self._tx = sender
        self._path = path
        self._fps = max(1, fps)
        self._w, self._h = width, height
        self._loop = loop
        self._link = link
        self._stop = threading.Event()

    def stop(self) -> None:
        self._stop.set()

    def release(self) -> None:
        """断开对 sender 的引用（线程必须已结束）。"""
        self._tx = None

    def run(self) -> None:
        try:
            import cv2
        except ImportError:
            self.failed.emit("没装 opencv-python，无法读视频文件（pip install opencv-python）")
            self.finishedRun.emit()
            return

        cap = cv2.VideoCapture(self._path)
        if not cap.isOpened():
            self.failed.emit("打不开视频文件：%s" % self._path)
            self.finishedRun.emit()
            return

        interval = 1.0 / self._fps
        next_t = time.time()
        sent = 0
        last_stats = 0.0
        try:
            while not self._stop.is_set():
                ok, frame = cap.read()
                if not ok:
                    if not self._loop:
                        break
                    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)   # 循环播放
                    continue
                if (frame.shape[1], frame.shape[0]) != (self._w, self._h):
                    frame = cv2.resize(frame, (self._w, self._h), interpolation=cv2.INTER_AREA)
                if not frame.flags["C_CONTIGUOUS"]:
                    frame = np.ascontiguousarray(frame)
                try:
                    self._tx.send_frame(frame)
                except RuntimeError as e:
                    self.failed.emit("发送失败：%s" % e)
                    break
                sent += 1

                # 预览：每帧都尝试，GUI 画得过来就出图（不按固定频率抽帧）
                if self._link.wants():
                    qi, dw, dh = bgr_to_qimage(frame)
                    if self._link.put(qi, (sent, self._w, self._h, dw, dh)):
                        self.frameAvailable.emit()

                now = time.time()
                if now - last_stats >= 0.5:
                    last_stats = now
                    try:
                        self.statsReady.emit(self._tx.stats)
                    except RuntimeError:
                        pass
                next_t += interval
                sleep = next_t - time.time()
                if sleep > 0:
                    time.sleep(sleep)
                elif sleep < -interval * 5:
                    next_t = time.time()      # 落后太多就重新对齐
            try:
                self._tx.flush()
                self.statsReady.emit(self._tx.stats)
            except RuntimeError:
                pass
        finally:
            cap.release()
            self.finishedRun.emit()


class VideoSendPanel(QGroupBox):
    """服务器面板的「图传发送」区。"""

    def __init__(self, default_host: str = "127.0.0.1", parent=None):
        super().__init__("图传发送（读视频文件 -> 编码 HEVC -> UDP）", parent)
        self._tx = None
        self._worker = None
        self._link = PreviewLink()
        self._shown = 0
        outer = QVBoxLayout(self)          # QGroupBox 默认没有布局，得自己建

        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignRight)

        row = QHBoxLayout()
        self.file_edit = QLineEdit()
        self.file_edit.setPlaceholderText("选择视频文件（mp4/avi/mkv…），也可手填路径")
        self.browse_btn = QPushButton("浏览…")
        self.browse_btn.clicked.connect(self._browse)
        row.addWidget(self.file_edit, 1)
        row.addWidget(self.browse_btn)
        w = QWidget(); w.setLayout(row)
        form.addRow("视频文件", w)

        row2 = QHBoxLayout()
        self.host_edit = QLineEdit(default_host)
        self.host_edit.setMaximumWidth(120)
        self.port_spin = QSpinBox(); self.port_spin.setRange(1, 65535); self.port_spin.setValue(3334)
        row2.addWidget(QLabel("目标"))
        row2.addWidget(self.host_edit)
        row2.addWidget(QLabel("端口"))
        row2.addWidget(self.port_spin)
        row2.addStretch(1)
        w2 = QWidget(); w2.setLayout(row2)
        form.addRow("发送到", w2)

        row3 = QHBoxLayout()
        self.w_spin = QSpinBox(); self.w_spin.setRange(64, 4096); self.w_spin.setValue(1920)
        self.w_spin.setSingleStep(16)
        self.h_spin = QSpinBox(); self.h_spin.setRange(64, 4096); self.h_spin.setValue(1080)
        self.h_spin.setSingleStep(16)
        self.fps_spin = QSpinBox(); self.fps_spin.setRange(1, 240); self.fps_spin.setValue(60)
        self.bitrate_spin = QSpinBox(); self.bitrate_spin.setRange(100, 100000)
        self.bitrate_spin.setValue(8000); self.bitrate_spin.setSuffix(" kbps")
        self.gop_spin = QSpinBox(); self.gop_spin.setRange(1, 600); self.gop_spin.setValue(60)
        self.loop_check = QCheckBox("循环"); self.loop_check.setChecked(True)
        for lbl, wdg in (("宽", self.w_spin), ("高", self.h_spin), ("帧率", self.fps_spin)):
            row3.addWidget(QLabel(lbl)); row3.addWidget(wdg)
        row3.addWidget(QLabel("码率")); row3.addWidget(self.bitrate_spin)
        row3.addWidget(QLabel("GOP")); row3.addWidget(self.gop_spin)
        row3.addWidget(self.loop_check)
        row3.addStretch(1)
        w3 = QWidget(); w3.setLayout(row3)
        form.addRow("编码", w3)

        row4 = QHBoxLayout()
        self.ir_check = QCheckBox("intra-refresh（丢包后画面自愈，低延迟链路常用）")
        self.aud_check = QCheckBox("AUD")
        self.preview_check = QCheckBox("显示预览")
        self.preview_check.setChecked(True)
        self.preview_check.setToolTip("显示正在编码发送的画面（尽量显示每一帧）。"
                                      "1080p 下关掉可以省一点开销。")
        self.preview_check.toggled.connect(self._on_preview_toggled)
        row4.addWidget(self.ir_check); row4.addWidget(self.aud_check)
        row4.addWidget(self.preview_check); row4.addStretch(1)
        w4 = QWidget(); w4.setLayout(row4)
        form.addRow("高级", w4)
        outer.addLayout(form)

        self.start_btn = QPushButton("开始发送图传")
        self.start_btn.clicked.connect(self.toggle)
        self.state_label = QLabel("未发送")
        self.stats_label = QLabel("—")
        self.stats_label.setWordWrap(True)
        bl = QHBoxLayout()
        bl.addWidget(self.start_btn)
        bl.addWidget(self.state_label)
        bl.addStretch(1)
        outer.addLayout(bl)

        outer.addWidget(QLabel("发送预览（正在编码发出的画面）"))
        self.preview = PreviewLabel("（未开始）")
        outer.addWidget(self.preview)
        self.preview_caption = QLabel("—")
        outer.addWidget(self.preview_caption)
        outer.addWidget(self.stats_label)

        # 只用来刷统计文字；画面由显示线程通过 frameAvailable 驱动
        self._timer = QTimer(self)
        self._timer.setInterval(500)
        self._timer.timeout.connect(self._tick_stats)
        self.refresh_theme()

    # -- 主题 --
    def refresh_theme(self) -> None:
        c = theme.palette_colors()
        self.preview.refresh_theme()
        self.stats_label.setStyleSheet("color: %s;" % c["muted"])
        self.preview_caption.setStyleSheet("color: %s;" % c["muted"])

    def changeEvent(self, ev):
        from PyQt5.QtCore import QEvent
        if ev.type() == QEvent.ApplicationPaletteChange:
            self.refresh_theme()
        super().changeEvent(ev)

    # -- 交互 --
    def _browse(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "选择视频文件", "", "视频 (*.mp4 *.avi *.mkv *.mov *.h264 *.h265);;所有文件 (*)")
        if path:
            self.file_edit.setText(path)

    def _set_editable(self, on: bool) -> None:
        # 注意：preview_check 不在这里 —— 预览开关是**运行中就该能随手切**的，
        # 禁掉它就等于这个功能没用（曾经就是这么错的）。
        for w in (self.file_edit, self.browse_btn, self.host_edit, self.port_spin,
                  self.w_spin, self.h_spin, self.fps_spin, self.bitrate_spin,
                  self.gop_spin, self.loop_check, self.ir_check, self.aud_check):
            w.setEnabled(on)
        if on:
            self.preview_check.setEnabled(True)

    def toggle(self) -> None:
        if self._worker is not None:
            self.stop()
        else:
            self.start()

    def start(self) -> None:
        path = self.file_edit.text().strip()
        if not path:
            self.state_label.setText("请先选择视频文件")
            return
        if not os.path.exists(path):
            self.state_label.setText("文件不存在：%s" % path)
            return
        w, h = self.w_spin.value(), self.h_spin.value()
        try:
            self._tx = rm.VideoSender(self.host_edit.text().strip() or "127.0.0.1",
                                      self.port_spin.value(), width=w, height=h,
                                      fps=self.fps_spin.value(),
                                      bitrate_kbps=self.bitrate_spin.value(),
                                      gop_size=self.gop_spin.value(),
                                      intra_refresh=self.ir_check.isChecked(),
                                      aud=self.aud_check.isChecked())
            self._tx.start()
        except (RuntimeError, MemoryError) as e:
            self.state_label.setText("启动失败：%s" % e)
            self._tx = None
            return

        self._link.clear()
        self._link.enabled = self.preview_check.isChecked()
        self._shown = 0
        # 故意不传 parent：QThread 一旦挂了 QObject parent，self._worker = None
        # 之后它仍作为面板的子对象存活，连带它引用的 sender/receiver 和
        # NumPy 缓冲一起泄漏（实测启停 20 次残留 118MB）。
        self._worker = SendWorker(self._tx, path, self.fps_spin.value(), w, h,
                                  self.loop_check.isChecked(), self._link)
        self._worker.statsReady.connect(self._on_stats)
        self._worker.frameAvailable.connect(self._on_frame_available)
        self._worker.failed.connect(lambda m: self.state_label.setText(m))
        self._worker.finishedRun.connect(self._on_worker_done)
        self._worker.start()

        self.start_btn.setText("停止图传")
        self.state_label.setText("发送中…")
        self._set_editable(False)
        self._timer.start()

    def stop(self) -> None:
        if self._worker is not None:
            w = self._worker
            w.stop()
            # 先把信号全断开：线程可能还要过一会儿才退出，这期间它在界面上
            # 排队的事件不该再影响已经「停止」的面板（迟到的 finishedRun
            # 甚至会把**下一轮**发送给停掉，已复现）。
            _disconnect_all(w)
            if not w.wait(THREAD_JOIN_MS):
                # 绝不用 terminate()：线程可能正持着 PreviewLink 的锁被杀死，
                # 锁会永久遗留，后面任何 clear()/take() 都会死等（已用故障注入复现）。
                # 也**不能**在这里把 self._worker 置空 —— 置空后第二次 stop()
                # 会认为「没有 worker」而直接去释放 sender，可线程还活着在用
                # 同一个对象（已复现：迟到帧照样画到已停止的界面上）。
                # 保留引用，让后续 stop()/窗口关闭复用同一条安全等待路径。
                _thread_stuck(self.state_label, "发送预览")
                return
            self._worker = None
            w.release()
            w.deleteLater()
        self._release()
        self._link.clear()
        self.start_btn.setText("开始发送图传")
        self.state_label.setText("未发送")
        self._set_editable(True)
        self._timer.stop()
        self.preview.clear_image("（未开始）")
        self.preview_caption.setText("—")

    @property
    def has_live_worker(self) -> bool:
        """是否还有没退出的工作线程。

        stop() 超时会**保留** worker 引用（见那里的说明），所以这个属性为真
        表示资源还没释放干净 —— 调用方（比如主窗口关闭事件）应据此拒绝退出。
        """
        return self._worker is not None

    def _on_preview_toggled(self, on: bool) -> None:
        # 运行中也能随手开关：显示线程每帧都会查这个标志，所以立刻生效
        self._link.enabled = on
        if not on:
            self._link.clear()
            self.preview.clear_image("（预览已关闭）")
            self.preview_caption.setText("—")

    def _on_frame_available(self) -> None:
        """GUI 线程：从槽里取最新一帧画出来（转换已经在显示线程做完）。

        注意：**无论预览开没开都必须先 take()**。
        历史教训：早期版本先判断开关再 return，于是槽里的帧和 `_notify_pending`
        标志一直留着；当时 `wants()` 还依赖「槽已空」，导致之后再也不生产新帧，
        预览**永久不再更新**（当时已复现）。
        现在 `wants()` 只看 `enabled`、不再依赖槽是否空，即使漏调 take() 也不会
        造成同样的死锁；但仍然保留「先 take()」的写法 —— 它保证通知标志一定被
        复位，是这条路径唯一的清理点，去掉会留下难以察觉的状态残留。
        """
        qi, meta = self._link.take()
        if qi is None:
            return
        if not self.preview_check.isChecked():
            return
        sent, src_w, src_h, dw, dh = meta
        self._shown += 1
        self.preview.set_image(qi, src_w, src_h)
        self.preview_caption.setText(
            _fmt_preview_caption("发送预览", sent, src_w, src_h, dw, dh))

    def _release(self) -> None:
        if self._tx is not None:
            try:
                self._tx.stop()
                self._tx.close()
            except Exception:
                pass
            self._tx = None

    def _on_worker_done(self) -> None:
        # 视频放完（未勾选循环）时自动收尾。
        # 必须确认信号来自**当前**worker：旧 worker 排队的事件若在重启之后
        # 才被处理，会把新一轮发送误停掉（已复现）。
        if self.sender() is not self._worker or self._worker is None:
            return
        if not self._worker._stop.is_set():
            self.stop()

    def _on_stats(self, st) -> None:
        shown = ""
        if self.preview_check.isChecked() and st.frames:
            shown = "   预览 %d/%d 帧" % (self._shown, st.frames)
        self.stats_label.setText(
            "已发 %d 帧 / %d 包 / %.1f KB   %.1f fps   %.2f Mbps%s%s"
            % (st.frames, st.packets, st.bytes / 1024.0, st.fps, st.mbps, shown,
               "" if st.encode_errors == 0 else "   编码错误 %d" % st.encode_errors))

    def _tick_stats(self) -> None:
        if self._tx is not None:
            try:
                self._on_stats(self._tx.stats)
            except RuntimeError:
                pass


# ===========================================================================
# 客户端侧：接收图传 + 实时预览
# ===========================================================================
CODEC_CHOICES = [
    ("自动探测（推荐）", rm.CODEC_AUTO),
    ("手动固定 H.265", rm.CODEC_H265),
    ("手动固定 H.264", rm.CODEC_H264),
    ("手动固定 JPEG", rm.CODEC_JPEG),
]


class DisplayWorker(QThread):
    """显示线程：持续从接收端取帧 -> 降采样 -> 转 QImage -> 交给 GUI。

    取帧用紧循环（空闲时 1ms 轮询），所以解码端每出一帧都会被拿到，
    不存在定时器采样那种「周期性与帧率错开就系统性丢帧」的问题。
    """

    frameAvailable = pyqtSignal()

    def __init__(self, receiver, link: PreviewLink, parent=None):
        super().__init__(parent)
        self._rx = receiver
        self._link = link
        self._stop = threading.Event()
        self.error = None

    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        while not self._stop.is_set():
            # 先问「现在要不要」，再去 C++ 取帧。
            # 顺序不能反：get_frame() 会把 C++ 的最新帧槽消费掉，
            # 如果取完才发现 GUI 忙而丢弃，槽里留下的反而是**更旧**的那一帧
            # （等于把「只保留最新帧」做成了「保留第一帧」）。
            if not self._link.wants():
                if self._stop.wait(POLL_IDLE_SEC):   # 可被打断的休眠
                    break
                continue
            try:
                f = self._rx.get_frame()
            except RuntimeError as e:
                self.error = str(e)
                break
            if f is None:
                if self._stop.wait(POLL_IDLE_SEC):
                    break
                continue
            qi, dw, dh = bgr_to_qimage(f.image)
            src_h, src_w = f.image.shape[:2]
            if self._link.put(qi, (f.frame_number, f.keyframe, src_w, src_h, dw, dh)):
                self.frameAvailable.emit()

    def release(self) -> None:
        """断开对 receiver 的引用，让缓冲能被回收（线程必须已结束）。"""
        self._rx = None


class VideoReceivePanel(QGroupBox):
    """客户端面板的「图传接收」区：实时预览 + 统计 + 解码格式模式。"""

    def __init__(self, default_host: str = "0.0.0.0", parent=None):
        super().__init__("图传接收（UDP -> 解码 -> 实时预览）", parent)
        self._rx = None
        self._display = None
        self._link = PreviewLink()
        outer = QVBoxLayout(self)          # QGroupBox 默认没有布局，得自己建

        grid = QGridLayout()
        grid.addWidget(QLabel("监听"), 0, 0)
        self.bind_edit = QLineEdit(default_host)
        self.bind_edit.setMaximumWidth(120)
        grid.addWidget(self.bind_edit, 0, 1)
        grid.addWidget(QLabel("端口"), 0, 2)
        self.port_spin = QSpinBox(); self.port_spin.setRange(1, 65535); self.port_spin.setValue(3334)
        grid.addWidget(self.port_spin, 0, 3)

        grid.addWidget(QLabel("解码格式"), 0, 4)
        self.codec_combo = QComboBox()
        for label, value in CODEC_CHOICES:
            self.codec_combo.addItem(label, value)
        self.codec_combo.currentIndexChanged.connect(self._on_codec_changed)
        grid.addWidget(self.codec_combo, 0, 5)

        self.start_btn = QPushButton("开始接收图传")
        self.start_btn.clicked.connect(self.toggle)
        grid.addWidget(self.start_btn, 0, 6)

        self.preview_check = QCheckBox("显示预览")
        self.preview_check.setChecked(True)
        self.preview_check.setToolTip("显示解码出来的画面（尽量显示每一帧）。"
                                      "关掉只统计不解码显示。")
        self.preview_check.toggled.connect(self._on_preview_toggled)
        grid.addWidget(self.preview_check, 0, 7)

        self.state_label = QLabel("未接收")
        grid.addWidget(self.state_label, 0, 8)
        grid.setColumnStretch(8, 1)
        outer.addLayout(grid)

        outer.addWidget(QLabel("接收预览（解码出来的画面）"))
        self.preview = PreviewLabel("（未开始）")
        outer.addWidget(self.preview, 1)

        self.preview_caption = QLabel("—")
        outer.addWidget(self.preview_caption)

        self.stats_label = QLabel("—")
        self.stats_label.setWordWrap(True)
        outer.addWidget(self.stats_label)

        # 统计文字用定时器刷新；画面由显示线程驱动，不经定时器
        self._stats_timer = QTimer(self)
        self._stats_timer.setInterval(500)
        self._stats_timer.timeout.connect(self._tick_stats)
        self._shown = 0
        self._last_frame_no = 0
        self.refresh_theme()

    def refresh_theme(self) -> None:
        c = theme.palette_colors()
        self.preview.refresh_theme()
        self.stats_label.setStyleSheet("color: %s;" % c["muted"])
        self.preview_caption.setStyleSheet("color: %s;" % c["muted"])

    def changeEvent(self, ev):
        from PyQt5.QtCore import QEvent
        if ev.type() == QEvent.ApplicationPaletteChange:
            self.refresh_theme()
        super().changeEvent(ev)

    @property
    def has_live_worker(self) -> bool:
        """是否还有没退出的显示线程（含义同 VideoSendPanel.has_live_worker）。"""
        return self._display is not None

    # -- 预览开关 --
    def _on_preview_toggled(self, on: bool) -> None:
        # 显示线程每帧都会查这个标志，所以运行中开关立刻生效
        self._link.enabled = on
        if not on:
            self._link.clear()
            self.preview.clear_image("（预览已关闭）")
            self.preview_caption.setText("—")

    # -- 解码格式模式 --
    def _on_codec_changed(self, _index: int) -> None:
        mode = self.codec_combo.currentData()
        if self._rx is None:
            return
        try:
            self._rx.set_codec_mode(mode)     # 运行中也能切
            if mode == rm.CODEC_AUTO:
                self.state_label.setText("接收中…（自动探测）")
            else:
                self.state_label.setText("接收中…（固定 %s）" % mode)
        except RuntimeError as e:
            self.state_label.setText("切换解码格式失败：%s" % e)

    # -- 生命周期 --
    def toggle(self) -> None:
        if self._rx is not None:
            self.stop()
        else:
            self.start()

    def start(self) -> None:
        mode = self.codec_combo.currentData()
        try:
            self._rx = rm.VideoReceiver(self.bind_edit.text().strip() or "0.0.0.0",
                                        self.port_spin.value(), codec_mode=mode)
            self._rx.start()
        except (RuntimeError, MemoryError) as e:
            self.state_label.setText("启动失败：%s" % e)
            self._rx = None
            return
        self.start_btn.setText("停止图传")
        self.state_label.setText("接收中…" + ("（自动探测）" if mode == rm.CODEC_AUTO
                                             else "（固定 %s）" % mode))
        self.bind_edit.setEnabled(False)
        self.port_spin.setEnabled(False)
        self._shown = 0
        self._link.clear()
        self._link.enabled = self.preview_check.isChecked()
        self._display = DisplayWorker(self._rx, self._link)   # 同上，不挂 parent
        self._display.frameAvailable.connect(self._on_frame_available)
        self._display.start()
        self._stats_timer.start()
        self.preview.clear_image("等待图传数据…")

    def stop(self) -> None:
        self._stats_timer.stop()
        if self._display is not None:
            w = self._display
            w.stop()
            _disconnect_all(w)      # 同上：先断开，迟到的帧不能再改已停止的界面
            if not w.wait(THREAD_JOIN_MS):
                _thread_stuck(self.state_label, "接收预览")
                return              # 保留 self._display 与 receiver，等下次 stop() 继续等
            self._display = None
            w.release()
            w.deleteLater()
        if self._rx is not None:
            try:
                self._rx.stop()
                self._rx.close()
            except Exception:
                pass
            self._rx = None
        self._link.clear()
        self.start_btn.setText("开始接收图传")
        self.state_label.setText("未接收")
        self.bind_edit.setEnabled(True)
        self.port_spin.setEnabled(True)
        self.preview.clear_image("（未开始）")
        self.preview_caption.setText("—")
        self.stats_label.setText("—")

    # -- 预览 --
    def _on_frame_available(self) -> None:
        """GUI 线程：从槽里取最新一帧画出来（转换已经在显示线程做完）。"""
        qi, meta = self._link.take()
        if qi is None:
            return
        frame_no, keyframe, src_w, src_h, dw, dh = meta
        self._last_frame_no = frame_no
        self.state_label.setText("接收中… 帧号 %d%s"
                                 % (frame_no, "（关键帧）" if keyframe else ""))
        if not self.preview_check.isChecked():
            return
        self.preview.set_image(qi, src_w, src_h)
        self._shown += 1      # 只在真的画出来之后才计数
        self.preview_caption.setText(
            _fmt_preview_caption("接收预览", frame_no, src_w, src_h, dw, dh,
                                 "，关键帧" if keyframe else ""))

    def _tick_stats(self) -> None:
        if self._rx is None:
            return
        try:
            st = self._rx.stats
        except RuntimeError:
            return
        mode_txt = "自动探测" if st.codec_mode == rm.CODEC_AUTO else "固定 %s" % st.codec_mode
        self.stats_label.setText(
            "%s/%s   %dx%d   收 %d 帧(完整) / 解 %d 帧   显示 %d 帧   %.1f fps   %.2f Mbps%s%s"
            % (st.codec, mode_txt, st.width, st.height, st.frames_complete,
               st.frames_decoded, self._shown, st.rx_fps, st.rx_mbps,
               "" if st.decode_errors == 0 else "   解码错误 %d" % st.decode_errors,
               "   [等待关键帧]" if st.waiting_keyframe else ""))
