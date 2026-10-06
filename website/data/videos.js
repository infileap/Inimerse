/* 实机演示视频 —— 唯一数据源（数据即记录，没有记录的视频不存在）
 *
 * 字段固定，由 video 会话按此交付，站点只按此渲染：
 *   id          string   稳定标识（同时用作封面文件名，如 demo-01）
 *   title       string   标题
 *   bvid        string   B站 BV 号；**未投稿就写 "未投稿"**（不要留空、不要编造）
 *   duration    string   时长，形如 "03:42"
 *   resolution  string   分辨率，形如 "1920x1080"
 *   cover       string   封面图，相对本站的路径（如 images/demo-01.jpg）或完整 URL
 *   summary     string   一句话简介
 *   ref         string   录制时的 git ref（`git rev-parse HEAD` 的输出）
 *   date        string   录制日期，形如 "2026-10-06"
 *   orientation string   可选，横屏 "landscape"（默认）/ 竖屏 "portrait"
 *   featured    boolean  可选，true 时占整行大卡
 *
 * 为什么不用 fetch 读 JSON：这个站点要能 file:// 直接打开，也要能整包上传到
 * B站 Toy（/toy/<slug>/ 子路径）。内联成 JS 全局变量就同时满足这两条。
 */
window.INFIVERSE_VIDEOS = [
  // 记录由 video 会话交付后追加到这里。示例形状（保持注释，避免渲染假数据）：
  // {
  //   id: "demo-01",
  //   title: "Inimerse 引擎实机演示 · 解释器与编译路径",
  //   bvid: "未投稿",
  //   duration: "03:42",
  //   resolution: "1920x1080",
  //   cover: "images/demo-01.jpg",
  //   summary: "一句话说明这个演示证明了什么。",
  //   ref: "f1dfe62583b7a89e2395ddc502fcf2ac7b60055e",
  //   date: "2026-10-06",
  //   orientation: "landscape",
  //   featured: true
  // },
];
