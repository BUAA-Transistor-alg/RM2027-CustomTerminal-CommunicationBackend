#!/usr/bin/env python3
"""离屏（无显示器）跑真实主窗口，验证：
  1. 服务器/客户端两个窗口能启动并互联
  2. 「启用 + 官方频率」能自动按频率发送（默认频率取自文档）
  3. 频率为 0 的指令变成「触发发送」按钮，点击才发
  4. 接收区实时显示，字段值正确
"""
import os, sys, time
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PyQt5.QtWidgets import QApplication
from gui import theme
from gui.main_window import MainWindow

PORT = int(os.environ.get("GUI_TEST_PORT", "13700"))
FAIL = []


def check(cond, msg):
    print(("  ✅ " if cond else "  ❌ ") + msg)
    if not cond:
        FAIL.append(msg)


def pump(app, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        app.processEvents()
        time.sleep(0.005)


def panel(win, name):
    for p in win.panels:
        if p.info.name == name:
            return p
    raise KeyError(name)


def main():
    app = QApplication(sys.argv)
    srv = MainWindow("server", "127.0.0.1", PORT)
    cli = MainWindow("client", "127.0.0.1", PORT, "3")
    srv.show(); cli.show()
    pump(app, 0.2)

    print("== 1. 启动互联 ==")
    srv.start_btn.click()
    pump(app, 0.3)
    cli.start_btn.click()
    pump(app, 1.0)
    check(srv.obj is not None and cli.obj is not None, "两个窗口都已启动")
    check(srv.obj.subscribed_client_count >= 1, "服务器看到客户端已订阅")

    print("== 2. 默认频率来自官方文档 ==")
    check(panel(srv, "GameStatus").frequency() == 5.0, "GameStatus 默认 5Hz")
    check(panel(srv, "RobotDynamicStatus").frequency() == 10.0, "RobotDynamicStatus 默认 10Hz")
    check(panel(cli, "KeyboardMouseControl").frequency() == 75.0, "KeyboardMouseControl 默认 75Hz")
    check(panel(cli, "DartCommand").frequency() == 0.0, "DartCommand（触发式）默认 0Hz")
    check(not panel(cli, "DartCommand").enable_box.isEnabled(), "频率为 0 时「启用」开关不可用")
    check(panel(cli, "DartCommand").send_btn.text() == "触发发送", "频率为 0 时按钮为「触发发送」")

    print("== 3. 按官方频率自动发送 ==")
    gs = panel(srv, "GameStatus")
    gs.set_data({"current_round": 1, "total_rounds": 3, "current_stage": 4,
                 "red_score": 777, "blue_score": 55, "game_result": 255})
    gs.enable_box.setChecked(True)
    rows_before = cli.receive._counts.get("GameStatus", 0)
    pump(app, 1.2)   # 5Hz -> 约 6 条
    got = cli.receive._counts.get("GameStatus", 0) - rows_before
    check(3 <= got <= 9, "1.2 秒内在 5Hz 下收到 %d 条 GameStatus（预期 4~8）" % got)

    # 发送内容确实带过去了
    txt = cli.receive.table.item(cli.receive._rows["GameStatus"], 2).text()
    check('"red_score": 777' in txt, "接收区显示的内容与发送一致: %s" % txt[:80])

    print("== 4. 触发式发送 ==")
    dart = panel(cli, "DartCommand")
    dart.set_data({"target_id": 1, "open": True, "launch_confirm": True})
    before = srv.obj.received
    dart.send_btn.click()
    pump(app, 0.6)
    check(srv.obj.received == before + 1, "点击「触发发送」后服务器收到 1 条")
    txt = srv.receive.table.item(srv.receive._rows["DartCommand"], 2).text()
    check('"target_id": 1' in txt and '"launch_confirm": true' in txt,
          "服务器接收区显示 DartCommand 内容: %s" % txt[:90])

    print("== 5. 停止自动发送 ==")
    gs.enable_box.setChecked(False)
    before = cli.receive._counts.get("GameStatus", 0)
    pump(app, 0.6)
    check(cli.receive._counts.get("GameStatus", 0) == before, "停止后不再收到新数据")

    print("== 6. 频率恢复官方默认 / 名称过滤 ==")
    gs.freq_box.setValue(99.0)
    srv._reset_freqs()
    check(gs.frequency() == 5.0, "「频率恢复官方默认」生效")
    cli.search_edit.setText("Dart")
    pump(app, 0.1)
    vis = [p.info.name for p in cli.panels if p.isVisible()]
    check(vis == ["DartCommand"], "按名称过滤只剩 DartCommand: %s" % vis)
    cli.search_edit.setText("")

    print("== 7. 截图 ==")
    outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    os.makedirs(outdir, exist_ok=True)
    cli.panels[0].expand_btn.setChecked(True)
    pump(app, 0.2)
    p1 = os.path.join(outdir, "gui_client_live.png")
    p2 = os.path.join(outdir, "gui_server_live.png")
    check(cli.grab().save(p1), "客户端截图已保存 %s" % p1)
    check(srv.grab().save(p2), "服务器截图已保存 %s" % p2)

    srv._stop(); cli._stop()
    pump(app, 0.2)

    print("== 8. 深色/浅色主题（回归：曾经白底白字看不见指令名）==")
    for dark in (True, False):
        name = "深色" if dark else "浅色"
        theme.apply_palette(app, dark)
        # 用新调色板重新建窗口，走真实构造路径
        ws = MainWindow("server", "127.0.0.1", PORT + 1)
        wc = MainWindow("client", "127.0.0.1", PORT + 1, "3")
        ws.show(); wc.show()
        pump(app, 0.25)

        c = theme.palette_colors()
        ratio = theme.contrast_ratio(c["panel"], c["text"])
        check(ratio >= 4.5,
              "%s主题：面板底 %s / 指令名 %s 对比度 %.1f:1（WCAG AA 要求 ≥4.5）"
              % (name, c["panel"], c["text"], ratio))

        import re
        ss = ws.styleSheet()
        m = re.search(r"background:\s*([^;]+);", ss)
        got_bg = (m.group(1).strip().lower() if m else "")
        check(got_bg == c["panel"].lower(),
              "%s主题：面板背景取自系统调色板（%s）" % (name, got_bg or "未设置"))
        # 关键回归：背景不能写死成浅色（否则深色主题下白底白字）
        is_light = theme.contrast_ratio(got_bg or "#000000", "#000000") > 10
        check(not (is_light and dark),
              "%s主题：背景不是写死的浅色（%s）" % (name, got_bg))

        # 面板标题控件本身也必须是可读的（深色下不该是黑字 / 浅色下不该是白字）
        title = ws.panels[0].findChild(type(ws.panels[0].doc_label), None)
        ws.panels[0].refresh_theme()
        check("rgba(" in ws.panels[0].doc_label.styleSheet()
              or theme.muted_color() in ws.panels[0].doc_label.styleSheet(),
              "%s主题：次要文字颜色已跟随主题刷新" % name)

        sfx = "dark" if dark else "light"
        q1 = os.path.join(outdir, "gui_server_%s.png" % sfx)
        q2 = os.path.join(outdir, "gui_client_%s.png" % sfx)
        check(ws.grab().save(q1) and wc.grab().save(q2),
              "%s主题截图已保存（%s / %s）" % (name, os.path.basename(q1), os.path.basename(q2)))
        ws._stop(); wc._stop()
        pump(app, 0.15)

    # ---------------------------------------------------------------- 9. 图传面板
    print()
    print("== 9. 图传面板（发送 / 接收 + 实时预览 + 解码格式模式）==")
    import threading
    import numpy as np
    import rm2027c as rm
    import gui.video_panel as vp
    from gui.video_panel import VideoReceivePanel, VideoSendPanel

    VIDEO_PORT = 14960
    W, H = 320, 180

    def _pattern(frame):
        bars = [(255, 255, 255), (0, 255, 255), (255, 255, 0), (0, 255, 0),
                (255, 0, 255), (0, 0, 255), (255, 0, 0), (64, 64, 64)]
        xs = np.arange(W)
        idx = (xs // (W // 8)) % 8
        img = np.zeros((H, W, 3), np.float64)
        for i, c in enumerate(bars):
            img[:, idx == i] = c
        return img.astype(np.uint8)

    # --- 接收面板 ---
    rxp = VideoReceivePanel("127.0.0.1")
    rxp.port_spin.setValue(VIDEO_PORT)
    rxp.show()
    rxp.start()
    check(rxp.state_label.text().startswith("接收中"), "接收面板已启动")
    check(not rxp.port_spin.isEnabled(), "运行中端口不可改")
    check(rxp.codec_combo.count() == 4, "解码格式有 4 个选项（自动/固定H265/H264/JPEG）")
    check(rxp.codec_combo.currentData() == rm.CODEC_AUTO, "默认解码格式 = 自动探测")

    stop = threading.Event()

    def _feed():
        tx = rm.VideoSender("127.0.0.1", VIDEO_PORT, width=W, height=H, fps=30,
                            bitrate_kbps=1500, gop_size=15)
        tx.start()
        i = 0
        while not stop.is_set() and i < 600:
            tx.send_frame(_pattern(i))
            i += 1
            time.sleep(1.0 / 30)
        tx.flush(); tx.stop(); tx.close()

    th = threading.Thread(target=_feed, daemon=True)
    th.start()
    # 画面现在由显示线程驱动（跨线程信号 -> GUI 线程的槽），
    # 所以只要把事件循环转起来就会自动更新；不需要再手动 tick。
    # 另外：整套测试会被 run_all.sh 串行跑，机器忙时时序会抖，
    # 所以**不要用固定秒数**做断言，一律「轮询等条件」。
    def pump_rx(sec):
        t = time.time()
        while time.time() - t < sec:
            pump(app, 0.01)

    def wait_rx(pred, timeout=6.0):
        t = time.time()
        while time.time() - t < timeout:
            pump(app, 0.01)
            if pred():
                return True
        return False

    wait_rx(lambda: rxp._shown > 20)          # 先让画面流起来

    # 关掉预览：不再渲染，但统计照常
    rxp.preview_check.setChecked(False)
    pump_rx(0.2)
    check(rxp.preview_caption.text() == "—", "关掉接收预览后标题清空")
    dec_before = rxp._rx.stats.frames_decoded
    got_more = wait_rx(lambda: rxp._rx.stats.frames_decoded > dec_before, timeout=5.0)
    check(got_more, "关掉预览后仍在解码统计（%d -> %d）"
                    % (dec_before, rxp._rx.stats.frames_decoded))
    rxp.preview_check.setChecked(True)
    check(wait_rx(lambda: "帧" in rxp.preview_caption.text(), timeout=5.0),
          "重新打开接收预览后恢复显示")

    wait_rx(lambda: rxp._shown > 40, timeout=3.0)

    # 性能回归防护（稳态窗口，增量测量）：
    # 必须只取「事件循环正常转」的一段来算比例 —— 探针改动里若夹着 GUI 停摆，
    # 停摆期间画不出来，比例会天然偏低，那样测的就不是抽帧问题了。
    _s0, _d0 = rxp._shown, rxp._rx.stats.frames_decoded
    pump_rx(1.0)
    _s1, _d1 = rxp._shown, rxp._rx.stats.frames_decoded
    _d_ratio = ((_s1 - _s0) / (_d1 - _d0)) if (_d1 - _d0) > 0 else 0.0
    check((_d1 - _d0) > 10 and _d_ratio >= 0.85,
          "接收预览不抽帧（稳态）：显示 +%d / 解码 +%d = %.0f%%（阈值 85%%）"
          % (_s1 - _s0, _d1 - _d0, _d_ratio * 100))

    # 「只保留最新帧」的**可观测语义**：GUI 停摆一段时间后恢复，
    # 第一个渲染出来的就必须是新帧，而不是停摆前那帧。
    # （只断言"最终能追上"是不够的 —— 曾经旧实现在这里会先闪一下旧画面。）
    rendered = []
    _orig_set = rxp.preview.set_image

    def _spy(qi, sw=0, sh=0):
        rendered.append(rxp._last_frame_no)   # 该值在 set_image 之前赋值
        return _orig_set(qi, sw, sh)

    rxp.preview.set_image = _spy
    pump_rx(0.2)
    n_before = len(rendered)
    t_stall = time.time()
    while time.time() - t_stall < 0.5:        # 停摆：不处理事件
        time.sleep(0.02)
    dec_at_resume = rxp._rx.stats.frames_decoded
    pump_rx(0.3)
    rxp.preview.set_image = _orig_set
    after = rendered[n_before:]
    first_after = after[0] if after else None
    check(first_after is not None and (dec_at_resume - first_after) <= 3,
          "GUI 停摆恢复后首帧即最新（恢复时已解码到 %s，首帧渲染 %s）"
          % (dec_at_resume, first_after))

    stop.set(); th.join(timeout=3)

    shown = rxp._shown
    st = rxp._rx.stats
    check(shown > 20, "预览显示了 %d 帧" % shown)
    check(st.frames_decoded > 20, "解码了 %d 帧" % st.frames_decoded)
    check(rxp.preview._pixmap is not None, "预览控件里有画面")
    check(st.codec == "h265", "自动模式探测到 h265")
    check(rxp.preview._pixmap is not None and rxp.preview._pixmap.width() > 0,
          "预览图像尺寸 %s" % (rxp.preview._pixmap.size().width()
                          if rxp.preview._pixmap else 0,))
    check("帧" in rxp.preview_caption.text(), "预览标题带帧号：%s" % rxp.preview_caption.text())

    # 运行中切换解码格式
    rxp.codec_combo.setCurrentIndex(1)
    pump(app, 0.05)
    check(rxp._rx.codec_mode == rm.CODEC_H265, "运行中切到手动固定 H.265")
    rxp.codec_combo.setCurrentIndex(0)
    pump(app, 0.05)
    check(rxp._rx.codec_mode == rm.CODEC_AUTO, "运行中切回自动探测")
    check(rxp.stats_label.text() != "—", "统计标签已更新")

    q = os.path.join(outdir, "gui_client_video_live.png")
    check(rxp.grab().save(q), "图传实时预览截图已保存（%s）" % os.path.basename(q))
    rxp.stop()
    check(rxp.port_spin.isEnabled(), "停止后端口可改")

    # --- 发送面板：用一个真实视频文件 ---
    clip = os.path.join(outdir, "test_clip.mp4")
    try:
        import cv2
        vw = cv2.VideoWriter(clip, cv2.VideoWriter_fourcc(*"mp4v"), 15, (W, H))
        for i in range(45):
            f = np.zeros((H, W, 3), np.uint8)
            f[:, :, 0] = (i * 5) % 256
            cv2.rectangle(f, (10 + i * 4, 20), (60 + i * 4, 80), (255, 255, 255), -1)
            vw.write(f)
        vw.release()
        has_cv2 = os.path.exists(clip)
    except Exception:
        has_cv2 = False
    check(has_cv2, "生成测试视频 test_clip.mp4")

    if has_cv2:
        VIDEO_PORT2 = VIDEO_PORT + 1
        rx2 = rm.VideoReceiver("127.0.0.1", VIDEO_PORT2, codec_mode=rm.CODEC_AUTO)
        rx2.start()
        txp = VideoSendPanel("127.0.0.1")
        txp.file_edit.setText(clip)
        txp.port_spin.setValue(VIDEO_PORT2)
        txp.w_spin.setValue(640); txp.h_spin.setValue(360)   # 故意与源不同 -> 走缩放
        txp.fps_spin.setValue(60)          # 60fps：用来验证预览不再被抽帧
        txp.show()
        txp.start()
        check(txp.state_label.text().startswith("发送中"), "发送面板已启动")
        check(not txp.file_edit.isEnabled(), "运行中视频文件不可改")
        # 预览开关必须在运行中可用 —— 它曾经被 _set_editable 一起禁掉，
        # 于是「运行中可随时开关」这个说法是假的（用 setChecked 测不出来）。
        check(txp.preview_check.isEnabled(), "运行中预览开关仍可点击")
        t0 = time.time()
        got = 0
        while time.time() - t0 < 3.0:
            pump(app, 0.01)
            if rx2.get_frame() is not None:
                got += 1
        q = os.path.join(outdir, "gui_server_video_live.png")
        check(txp.grab().save(q), "图传发送面板截图已保存（运行中，%s）" % os.path.basename(q))
        check(txp.preview._pixmap is not None, "发送面板运行中也能看到预览")

        # 运行中开关预览：立刻生效
        txp.preview_check.setChecked(False)
        pump(app, 0.2)
        check(txp.preview_caption.text() == "—", "关掉发送预览后标题清空")
        txp.preview_check.setChecked(True)
        tprev = time.time()
        while time.time() - tprev < 5.0 and "帧" not in txp.preview_caption.text():
            pump(app, 0.01)
        check("帧" in txp.preview_caption.text(), "重新打开发送预览后恢复显示")

        # 性能回归防护：预览是「尽量显示」，显示帧数应当接近发送帧数。
        # 曾经的实现把预览硬限在 15fps（60fps 流下只有 25%）——
        # 那种退化必须让这条断言失败。（只断言"显示过若干帧"是抓不住的。）
        sent_now = txp._tx.stats.frames
        shown_now = txp._shown
        ratio = (shown_now / sent_now) if sent_now else 0.0
        check(sent_now > 60 and ratio >= 0.85,
              "发送预览不抽帧：显示 %d / 发送 %d = %.0f%%（阈值 85%%）"
              % (shown_now, sent_now, ratio * 100))

        txp.stop()
        st2 = rx2.stats
        check(got > 10, "收到 %d 帧" % got)
        check(st2.width == 640 and st2.height == 360,
              "源 %dx%d 已按面板设置缩放到 %dx%d" % (W, H, st2.width, st2.height))
        check(txp.file_edit.isEnabled(), "停止后视频文件可改")
        check(txp.stats_label.text() != "—", "发送统计标签已更新")
        check(txp.preview_caption.text() == "—", "停止后预览标题清空")
        check(txp._worker is None and txp._display is None
              if hasattr(txp, "_display") else txp._worker is None,
              "停止后不再持有 worker 引用")

        # 反复启停：worker 曾经因为挂了 QObject parent 而永不释放，
        # 连带 sender/receiver 和 NumPy 缓冲一起泄漏（实测 20 次残留 118MB）。
        import gc
        for _ in range(5):
            txp.start(); pump(app, 0.15); txp.stop()
        gc.collect(); pump(app, 0.1)
        leftovers = [o for o in gc.get_objects()
                     if o.__class__.__name__ == "SendWorker"]
        check(len(leftovers) == 0,
              "反复启停 5 次后无残留 SendWorker（实得 %d）" % len(leftovers))
        rx2.stop(); rx2.close()

    # ---------------------------------------------------------------- 10. 窗口关闭路径
    print()
    print("== 10. 主窗口关闭时的图传生命周期 ==")
    # 注意：MainWindow 已在模块顶部导入过。这里**不要**再 import 一次 ——
    # 函数内的 import 会让它在整个 main() 里变成局部变量，
    # 于是前面第 42 行的用法会报 UnboundLocalError（已踩过）。
    from PyQt5.QtGui import QCloseEvent

    WIN_PORT = VIDEO_PORT + 20
    win = MainWindow("client", "127.0.0.1", WIN_PORT, "3")
    win.resize(1200, 800)
    win.show()
    win.video.port_spin.setValue(WIN_PORT)
    win.video.start()
    pump(app, 0.2)

    feed_stop = threading.Event()

    def _feed_win():
        tx = rm.VideoSender("127.0.0.1", WIN_PORT, width=W, height=H, fps=30,
                            bitrate_kbps=1500, gop_size=15)
        tx.start()
        i = 0
        while not feed_stop.is_set() and i < 200:
            tx.send_frame(_pattern(i))
            i += 1
            time.sleep(1.0 / 30)
        tx.flush(); tx.stop(); tx.close()

    thw = threading.Thread(target=_feed_win, daemon=True)
    thw.start()
    t0 = time.time()
    while time.time() - t0 < 1.5:
        pump(app, 0.02)

    # 这个类历史上定义过两个 closeEvent，后一个覆盖前一个，
    # 于是关窗口时图传线程根本不会被停（测试只调面板 stop()，发现不了）。
    check(win.video._display is not None, "关闭前显示线程在运行")

    ev = QCloseEvent()
    win.closeEvent(ev)
    pump(app, 0.2)
    check(win.video._display is None and not win.video.has_live_worker,
          "窗口关闭后图传显示线程已释放")
    check(ev.isAccepted(), "正常情况下关闭被接受")
    feed_stop.set(); thw.join(timeout=3)

    # 线程退不出来时必须拒绝关闭（否则线程会用已被销毁的界面对象）
    check(hasattr(vp, "THREAD_JOIN_MS"), "THREAD_JOIN_MS 可调（便于测试超时路径）")
    old_join = vp.THREAD_JOIN_MS
    vp.THREAD_JOIN_MS = 200
    win2 = MainWindow("client", "127.0.0.1", WIN_PORT + 1, "4")
    win2.show()
    blocked = threading.Event()
    released2 = threading.Event()

    class _BlockRx:
        def __init__(self, real):
            self._r = real
        def get_frame(self):
            blocked.set(); released2.wait(5.0)
            return self._r.get_frame()
        def __getattr__(self, n):
            return getattr(self._r, n)

    real_rx = rm.VideoReceiver("127.0.0.1", WIN_PORT + 2, codec_mode=rm.CODEC_AUTO)
    real_rx.start()
    w_worker = vp.DisplayWorker(_BlockRx(real_rx), vp.PreviewLink())
    win2.video._rx = real_rx
    win2.video._display = w_worker
    w_worker.start()
    blocked.wait(3.0)
    ev2 = QCloseEvent()
    win2.closeEvent(ev2)
    check(not ev2.isAccepted(), "图传线程未退出时拒绝关闭")
    check(win2.video.has_live_worker, "拒绝关闭时保留 worker（不强杀、不释放）")
    released2.set()
    time.sleep(0.3); pump(app, 0.1)
    ev3 = QCloseEvent()
    win2.closeEvent(ev3)
    check(ev3.isAccepted() and not win2.video.has_live_worker,
          "线程退出后再次关闭被接受且资源释放")
    vp.THREAD_JOIN_MS = old_join

    print()
    if FAIL:
        print("❌ GUI 测试失败 %d 项:" % len(FAIL))
        for f in FAIL:
            print("   -", f)
        return 1
    print("✅ GUI（PyQt5）离屏功能测试全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
