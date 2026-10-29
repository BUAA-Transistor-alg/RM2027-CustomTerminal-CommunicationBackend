"""按指令的字段表自动生成编辑控件，并在「控件 <-> dict」之间转换。

字段表来自 C 接口（官方 PDF 字段表）：
    {"name": "current_round", "type": "uint32", "num": 1, "repeated": false}
    嵌套消息额外带 "fields": [...]
"""
from __future__ import annotations

import base64
import json
from typing import Any, Dict, List

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (QCheckBox, QHBoxLayout, QLabel, QLineEdit,
                             QPlainTextEdit, QSizePolicy, QVBoxLayout, QWidget)

INT_TYPES = {"int32", "uint32", "int64", "uint64", "sint32", "sint64",
             "fixed32", "fixed64", "sfixed32", "sfixed64"}
FLOAT_TYPES = {"float", "double"}


# --------------------------------------------------------------------- 工具
def bytes_to_text(b64: str) -> str:
    """protobuf JSON 的 bytes 是 base64；界面上同时给出可读预览"""
    try:
        raw = base64.b64decode(b64 or "", validate=False)
    except Exception:
        return b64 or ""
    printable = all(32 <= b < 127 for b in raw)
    if printable and raw:
        return '%s  (hex: %s)' % (raw.decode("ascii"), raw.hex(" "))
    return "hex: " + raw.hex(" ") if raw else "(空)"


def parse_bytes_input(text: str) -> str:
    """界面输入 -> base64。

    支持三种写法：
        hex:00 11 22 / 0x001122   -> 按十六进制解析
        纯文本                     -> 按 UTF-8 编码后 base64
    """
    t = text.strip()
    if not t:
        return ""
    low = t.lower()
    if low.startswith("hex:"):
        t = t[4:].strip()
    elif low.startswith("0x"):
        t = t[2:].strip()
    else:
        return base64.b64encode(t.encode("utf-8")).decode("ascii")
    cleaned = t.replace(" ", "").replace(",", "").replace("_", "")
    if len(cleaned) % 2 != 0:
        raise ValueError("十六进制长度必须为偶数: %r" % t)
    return base64.b64encode(bytes.fromhex(cleaned)).decode("ascii")


def default_value(field: Dict[str, Any]) -> Any:
    if "fields" in field:
        return [] if field.get("repeated") else {f["name"]: default_value(f) for f in field["fields"]}
    t = field["type"]
    if field.get("repeated"):
        return []
    if t == "bool":
        return False
    if t in INT_TYPES:
        return 0
    if t in FLOAT_TYPES:
        return 0.0
    return ""


def empty_message(fields: List[dict]) -> Dict[str, Any]:
    return {f["name"]: default_value(f) for f in fields}


# --------------------------------------------------------------------- 编辑器
class FieldEditor(QWidget):
    """单个字段的编辑控件"""

    def __init__(self, field: Dict[str, Any], parent=None):
        super().__init__(parent)
        self.field = field
        self.name = field["name"]
        self.type = field["type"]
        self.repeated = bool(field.get("repeated"))
        self.is_nested = "fields" in field

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)

        label = QLabel(self._label_text())
        label.setMinimumWidth(178)
        label.setMaximumWidth(210)
        label.setToolTip(self._tooltip())
        lay.addWidget(label)

        self.editor = self._make_editor()
        lay.addWidget(self.editor, 1)

    # ---- 显示 ----
    def _label_text(self) -> str:
        t = self.type
        if self.is_nested:
            t = self.type
        if self.repeated:
            t = "repeated " + t
        return "%s  (%s)" % (self.name, t)

    def _tooltip(self) -> str:
        if self.type == "bytes":
            return "bytes：可直接输入文本（按 UTF-8 编码），或用 hex:00 11 22 指定原始字节"
        if self.is_nested:
            return "嵌套消息：填 JSON %s" % ("数组" if self.repeated else "对象")
        if self.repeated:
            return "数组：用逗号或空格分隔，例如 1 2 3"
        if self.type == "bool":
            return "true / false"
        return "字段编号 %s" % self.field.get("num", "?")

    def _make_editor(self):
        if self.is_nested:
            e = QPlainTextEdit()
            e.setPlaceholderText("[{...}]" if self.repeated else "{...}")
            e.setFixedHeight(60)
            e.setTabChangesFocus(True)
            return e
        if self.type == "bool" and not self.repeated:
            return QCheckBox()
        e = QLineEdit()
        e.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        if self.type == "bytes":
            e.setPlaceholderText("文本，或 hex:00 11 22")
        elif self.repeated:
            e.setPlaceholderText("多个值用空格/逗号分隔")
        return e

    # ---- 取值 / 赋值 ----
    def value(self) -> Any:
        if self.is_nested:
            text = self.editor.toPlainText().strip()
            if not text:
                return [] if self.repeated else {}
            return json.loads(text)
        if isinstance(self.editor, QCheckBox):
            return self.editor.isChecked()

        text = self.editor.text().strip()
        if self.repeated:
            parts = [p for p in text.replace(",", " ").split() if p]
            return [self._convert_one(p) for p in parts]
        return self._convert_one(text)

    def _convert_one(self, text: str) -> Any:
        if self.type == "bytes":
            return parse_bytes_input(text)
        if self.type in INT_TYPES:
            v = int(float(text)) if text else 0
            if self.type.startswith("u") and v < 0:
                raise ValueError("%s 不能为负数" % self.type)
            return v
        if self.type in FLOAT_TYPES:
            return float(text) if text else 0.0
        return text

    def set_value(self, v: Any) -> None:
        if self.is_nested:
            self.editor.setPlainText(json.dumps(v or ([] if self.repeated else {}),
                                               ensure_ascii=False))
            return
        if isinstance(self.editor, QCheckBox):
            self.editor.setChecked(bool(v))
            return
        if self.repeated:
            if isinstance(v, list):
                self.editor.setText(" ".join(str(x) for x in v))
            else:
                self.editor.setText(str(v))
            return
        if self.type == "bytes" and isinstance(v, str):
            self.editor.setText(v)  # 界面直接编辑 base64 也允许
            return
        self.editor.setText("" if v is None else str(v))


def build_form(fields: List[dict]) -> List[FieldEditor]:
    return [FieldEditor(f) for f in fields]
