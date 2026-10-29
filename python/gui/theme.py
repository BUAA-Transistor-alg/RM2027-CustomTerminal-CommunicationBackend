"""跟随系统调色板的主题工具。

之前把面板背景写死成 #fafafa，在深色主题下文字会变成白色 -> 白底白字看不见。
这里统一从 QPalette 取色，明暗主题都能正确显示。
"""
from __future__ import annotations

from PyQt5.QtGui import QColor, QPalette
from PyQt5.QtWidgets import QApplication


def is_dark() -> bool:
    """当前是否为深色主题"""
    pal = QApplication.palette()
    return pal.color(QPalette.Window).lightness() < 128


def _rgba(color, alpha: int) -> str:
    return "rgba(%d,%d,%d,%d)" % (color.red(), color.green(), color.blue(), alpha)


def muted_color() -> str:
    """次要文字：用前景色加透明度，明暗主题下都清晰"""
    pal = QApplication.palette()
    c = pal.color(QPalette.WindowText)
    return _rgba(c, 150)


def success_color() -> str:
    return "#5fd35f" if is_dark() else "#0a7a0a"


def error_color() -> str:
    return "#ff7070" if is_dark() else "#b00020"


def contrast_ratio(color_a: str, color_b: str) -> float:
    """WCAG 对比度，用于自动校验「文字看得见」"""
    def lum(name: str) -> float:
        c = QColor(name)
        parts = []
        for v in (c.redF(), c.greenF(), c.blueF()):
            parts.append(v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4)
        return 0.2126 * parts[0] + 0.7152 * parts[1] + 0.0722 * parts[2]

    la, lb = lum(color_a), lum(color_b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def palette_colors() -> dict:
    """当前主题下用到的颜色。单独抽出来是为了让测试能直接校验对比度。"""
    pal = QApplication.palette()
    alt = pal.color(QPalette.AlternateBase).name()
    base = pal.color(QPalette.Base).name()
    window = pal.color(QPalette.Window).name()
    # 深色主题下 AlternateBase 可能与 Window 很接近，用 Base 兜底保证能分辨
    panel_bg = alt if alt.lower() != window.lower() else base
    return {
        "panel": panel_bg,
        "text": pal.color(QPalette.WindowText).name(),
        "muted": muted_color(),
        "mid": pal.color(QPalette.Mid).name(),
        "highlight": pal.color(QPalette.Highlight).name(),
    }


def build_stylesheet() -> str:
    """面板等自定义样式：全部取自调色板，不写死颜色"""
    c = palette_colors()
    return """
QFrame#messagePanel {
    background: %(panel)s;
    border: 1px solid %(mid)s;
    border-radius: 4px;
}
QFrame#messagePanel:hover { border-color: %(highlight)s; }
QFrame#messagePanel QLabel { color: %(text)s; }
QPushButton { padding: 3px 10px; }
""" % c


def apply_palette(app, dark: bool) -> None:
    """强制切换到深色/浅色主题（用户没开系统深色模式时也能用）"""
    from PyQt5.QtWidgets import QStyleFactory
    pal = QPalette()
    if dark:
        app.setStyle(QStyleFactory.create("Fusion"))
        pal.setColor(QPalette.Window, QColor(53, 53, 53))
        pal.setColor(QPalette.WindowText, QColor(255, 255, 255))
        pal.setColor(QPalette.Base, QColor(35, 35, 35))
        pal.setColor(QPalette.AlternateBase, QColor(53, 53, 53))
        pal.setColor(QPalette.Text, QColor(255, 255, 255))
        pal.setColor(QPalette.Button, QColor(53, 53, 53))
        pal.setColor(QPalette.ButtonText, QColor(255, 255, 255))
        pal.setColor(QPalette.Highlight, QColor(42, 130, 218))
        pal.setColor(QPalette.HighlightedText, QColor(0, 0, 0))
        pal.setColor(QPalette.Mid, QColor(90, 90, 90))
        pal.setColor(QPalette.Disabled, QPalette.Text, QColor(130, 130, 130))
        pal.setColor(QPalette.Disabled, QPalette.ButtonText, QColor(130, 130, 130))
    else:
        app.setStyle(QStyleFactory.create("Fusion"))
        pal.setColor(QPalette.Window, QColor(240, 240, 240))
        pal.setColor(QPalette.WindowText, QColor(0, 0, 0))
        pal.setColor(QPalette.Base, QColor(255, 255, 255))
        pal.setColor(QPalette.AlternateBase, QColor(247, 247, 247))
        pal.setColor(QPalette.Text, QColor(0, 0, 0))
        pal.setColor(QPalette.Button, QColor(240, 240, 240))
        pal.setColor(QPalette.ButtonText, QColor(0, 0, 0))
        pal.setColor(QPalette.Highlight, QColor(48, 140, 198))
        pal.setColor(QPalette.HighlightedText, QColor(255, 255, 255))
        pal.setColor(QPalette.Mid, QColor(184, 184, 184))
    app.setPalette(pal)
