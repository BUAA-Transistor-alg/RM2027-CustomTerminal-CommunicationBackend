#!/usr/bin/env python3
"""RM2027 自定义终端控制面板（PyQt5）

  python3 python/run_client.py --host 127.0.0.1 --port 3333 --client-id 3
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PyQt5.QtWidgets import QApplication  # noqa: E402

from gui.main_window import MainWindow  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description="RM2027 自定义终端控制面板")
    ap.add_argument("--host", default="127.0.0.1",
                    help="broker 地址（官方现场为 192.168.12.1）")
    ap.add_argument("--port", type=int, default=3333, help="broker 端口（官方 3333）")
    ap.add_argument("--client-id", default="3",
                    help="MQTT clientID = 机器人 ID"
                         "（红方：1重装 3/4/5步兵 6空中系统 7哨兵；"
                         "蓝方 = 红方 +100，2/102 为保留位）")
    ap.add_argument("--lib", default=None, help="librm2027c.so 路径（可省）")
    ap.add_argument("--dark", action="store_true",
                    help="强制深色主题（默认跟随系统）")
    ap.add_argument("--light", action="store_true",
                    help="强制浅色主题（默认跟随系统）")
    args = ap.parse_args()

    if args.lib:
        os.environ["RM2027C_LIB"] = args.lib

    app = QApplication(sys.argv)
    from gui import theme
    if args.dark:
        theme.apply_palette(app, True)
    elif args.light:
        theme.apply_palette(app, False)
    w = MainWindow("client", args.host, args.port, args.client_id)
    w.show()
    return app.exec_()


if __name__ == "__main__":
    sys.exit(main())
