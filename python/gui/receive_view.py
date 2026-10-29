"""接收数据的实时显示：一张「每条指令最新状态」表 + 一个滚动日志。"""
from __future__ import annotations

import json
import time
from typing import Dict, List

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont
from PyQt5.QtWidgets import (QHeaderView, QLabel, QPlainTextEdit, QSplitter,
                             QTableWidget, QTableWidgetItem, QVBoxLayout,
                             QWidget)

from . import theme
from rm2027c import MessageInfo


class ReceiveView(QWidget):
    """实时显示某个方向的接收数据"""

    def __init__(self, infos: List[MessageInfo], parent=None):
        super().__init__(parent)
        self.infos = {m.name: m for m in infos}
        self._counts: Dict[str, int] = {}
        self._rows: Dict[str, int] = {}

        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 4, 4, 4)
        lay.setSpacing(4)

        self.summary = QLabel("尚未收到数据")
        self.summary.setStyleSheet("color:%s;" % theme.muted_color())
        lay.addWidget(self.summary)

        split = QSplitter(Qt.Vertical)

        self.table = QTableWidget(0, 3)
        self.table.setHorizontalHeaderLabels(["指令", "次数", "最新内容 (JSON)"])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(1, QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(2, QHeaderView.Stretch)
        split.addWidget(self.table)

        log_wrap = QWidget()
        log_lay = QVBoxLayout(log_wrap)
        log_lay.setContentsMargins(0, 0, 0, 0)
        log_lay.addWidget(QLabel("滚动日志"))
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(2000)
        self.log.setFont(QFont("Monospace", 9))
        log_lay.addWidget(self.log)
        split.addWidget(log_wrap)
        split.setSizes([500, 220])
        lay.addWidget(split, 1)

        for name, info in self.infos.items():
            r = self.table.rowCount()
            self.table.insertRow(r)
            self._rows[name] = r
            self._counts[name] = 0
            item = QTableWidgetItem(name)
            item.setToolTip("官方频率: %s" % info.frequency_text)
            self.table.setItem(r, 0, item)
            self.table.setItem(r, 1, QTableWidgetItem("0"))
            self.table.setItem(r, 2, QTableWidgetItem(""))

    def refresh_theme(self) -> None:
        self.summary.setStyleSheet("color:%s;" % theme.muted_color())

    # ------------------------------------------------------------------
    def on_message(self, name: str, data: dict, client_id: str = "") -> None:
        if name not in self._rows:
            return
        self._counts[name] = self._counts.get(name, 0) + 1
        r = self._rows[name]
        self.table.item(r, 1).setText(str(self._counts[name]))
        text = json.dumps(data, ensure_ascii=False)
        self.table.item(r, 2).setText(text)
        self.table.item(r, 2).setToolTip(text)

        ts = time.strftime("%H:%M:%S")
        prefix = "%s  %-34s" % (ts, name)
        if client_id:
            prefix += " [%s]" % client_id
        self.log.appendPlainText("%s  %s" % (prefix, text))

        total = sum(self._counts.values())
        kinds = sum(1 for c in self._counts.values() if c > 0)
        self.summary.setText("累计收到 %d 条，覆盖 %d/%d 种指令" % (total, kinds, len(self._rows)))

    def clear(self) -> None:
        for name in self._counts:
            self._counts[name] = 0
            r = self._rows[name]
            self.table.item(r, 1).setText("0")
            self.table.item(r, 2).setText("")
        self.log.clear()
        self.summary.setText("已清空")
