#!/usr/bin/env python3
"""离屏渲染主窗口并截图（用于无显示器环境下检查界面布局）"""
import os, sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PyQt5.QtGui import QColor, QPalette
from PyQt5.QtWidgets import QApplication
from gui.main_window import MainWindow


def apply_dark(app):
    """模拟系统深色主题（与 KDE/GNOME 深色配色接近）"""
    from PyQt5.QtWidgets import QStyleFactory
    app.setStyle(QStyleFactory.create("Fusion"))
    p = QPalette()
    p.setColor(QPalette.Window, QColor(53, 53, 53))
    p.setColor(QPalette.WindowText, QColor(255, 255, 255))
    p.setColor(QPalette.Base, QColor(35, 35, 35))
    p.setColor(QPalette.AlternateBase, QColor(53, 53, 53))
    p.setColor(QPalette.Text, QColor(255, 255, 255))
    p.setColor(QPalette.Button, QColor(53, 53, 53))
    p.setColor(QPalette.ButtonText, QColor(255, 255, 255))
    p.setColor(QPalette.Highlight, QColor(42, 130, 218))
    p.setColor(QPalette.HighlightedText, QColor(0, 0, 0))
    p.setColor(QPalette.Mid, QColor(90, 90, 90))
    p.setColor(QPalette.Disabled, QPalette.Text, QColor(130, 130, 130))
    p.setColor(QPalette.Disabled, QPalette.ButtonText, QColor(130, 130, 130))
    app.setPalette(p)

def main():
    role = sys.argv[1] if len(sys.argv) > 1 else "server"
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/%s.png" % role
    app = QApplication(sys.argv)
    if "--dark" in sys.argv:
        apply_dark(app)
    w = MainWindow(role, "127.0.0.1", 3333, "3")
    w.resize(1480, 880)
    w.show()
    for _ in range(6):
        app.processEvents()
    # 展开第一个面板，展示字段编辑区
    if w.panels:
        w.panels[0].expand_btn.setChecked(True)
    for _ in range(6):
        app.processEvents()
    ok = w.grab().save(out)
    print("保存", out, ok)
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
