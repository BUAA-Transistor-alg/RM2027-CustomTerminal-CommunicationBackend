/* 纯 C 接口的图传测试：不用任何 C++ 语法，验证 capi 完整可用 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rm2027/capi/rm2027_c.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  ✅ %s\n", msg); } \
    else { printf("  ❌ %s\n", msg); failures++; } \
} while (0)

int main(void) {
    printf("== 纯 C 接口图传测试 ==\n");
    printf("版本: %s\n", rm_version());

    /* ---- 接收端先起来 ---- */
    rm_handle rx = rm_video_receiver_new("127.0.0.1", 14700);
    CHECK(rx != NULL, "创建接收端句柄");
    CHECK(rm_video_receiver_set_codec_mode(rx, RM_VIDEO_CODEC_AUTO) == 0, "设置解码格式模式=auto");
    CHECK(strcmp(rm_video_receiver_codec_mode(rx), "auto") == 0, "读回模式字符串=auto");
    CHECK(rm_video_receiver_set_codec_mode(rx, "H.265") == 0, "接受 H.265 这种写法");
    CHECK(rm_video_receiver_set_codec_mode(rx, "bogus") != 0, "拒绝无法识别的模式");
    rm_video_receiver_set_codec_mode(rx, RM_VIDEO_CODEC_AUTO);

    if (rm_video_receiver_start(rx) != 0) {
        printf("  ❌ 启动接收端: %s\n", rm_last_error());
        return 1;
    }
    CHECK(rm_video_receiver_is_running(rx) == 1, "接收端已启动");

    /* ---- 发送端：先用 JSON 配编码参数 ---- */
    rm_handle tx = rm_video_sender_new("127.0.0.1", 14700);
    CHECK(tx != NULL, "创建发送端句柄");
    CHECK(rm_video_sender_config(tx,
              "{\"width\":640,\"height\":360,\"fps\":30,\"bitrate_kbps\":2000,"
              "\"gop_size\":15,\"intra_refresh\":false,\"preset\":\"ultrafast\"}") == 0,
          "用 JSON 配置编码参数");
    CHECK(rm_video_sender_start(tx) == 0, "启动发送端");
    CHECK(rm_video_sender_is_running(tx) == 1, "发送端已启动");

    /* ---- 收发交错：喂 30 帧，同时不停取帧（这才是真实的预览用法）---- */
    const int W = 640, H = 360;
    uint8_t *img = (uint8_t *)malloc((size_t)W * H * 3);
    uint8_t *buf = (uint8_t *)malloc(1920 * 1080 * 3);
    char stats[4096];
    uint64_t since = 0, fno = 0;
    int w = 0, h = 0, stride = 0, key = 0, got = 0, sent = 0;

    for (int i = 0; i < 240; i++) {
        if (i % 4 == 0 && sent < 30) {           /* 每 4 轮发一帧 ≈ 30fps */
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    uint8_t *p = img + ((size_t)y * W + x) * 3;
                    p[0] = (uint8_t)(x * 255 / W);   /* B */
                    p[1] = (uint8_t)(y * 255 / H);   /* G */
                    p[2] = (uint8_t)(sent * 8);      /* R：随帧变化 */
                }
            }
            if (rm_video_sender_send_bgr(tx, img, W, H, W * 3) != 0) {
                printf("  ❌ 发送第 %d 帧: %s\n", sent, rm_last_error());
                break;
            }
            sent++;
        }
        int r = rm_video_receiver_get_frame(rx, buf, 1920 * 1080 * 3, &w, &h, &stride, &fno,
                                            &key, &since);
        if (r == 1) got++;
        else if (r == -1) { printf("  ❌ get_frame 出错: %s\n", rm_last_error()); break; }
        usleep(8000);
    }
    CHECK(rm_video_sender_flush(tx) == 0, "冲刷编码器");
    CHECK(rm_video_sender_stats(tx, stats, sizeof(stats)) > 0, "读发送端统计");
    printf("     发送端: %s\n", stats);
    CHECK(got > 5, "成功取到多帧解码画面（最新帧槽位语义）");
    printf("     取到 %d 帧，尺寸 %dx%d，最后一帧号 %llu\n", got, w, h,
           (unsigned long long)fno);
    CHECK(w == W && h == H, "解码分辨率与发送一致");
    CHECK(rm_video_receiver_stats(rx, stats, sizeof(stats)) > 0, "读接收端统计");
    printf("     接收端: %s\n", stats);
    CHECK(strstr(stats, "\"codec\":\"h265\"") != NULL, "自动模式探测到 h265");
    CHECK(strstr(stats, "\"codecMode\":\"auto\"") != NULL, "统计里带 codecMode");

    CHECK(rm_video_receiver_set_codec_mode(rx, "h264") == 0, "运行中切换到手动固定 h264");
    CHECK(strcmp(rm_video_receiver_codec_mode(rx), "h264") == 0, "运行中模式已切换");

    rm_video_sender_stop(tx);
    rm_video_sender_free(tx);
    rm_video_receiver_stop(rx);
    rm_video_receiver_free(rx);
    free(img);
    free(buf);

    printf("\n%s\n", failures == 0 ? "✅ 纯 C 图传接口测试全部通过" : "❌ 有失败项");
    return failures == 0 ? 0 : 1;
}
