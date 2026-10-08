/* 在线游戏 / 互动作品 —— 唯一数据源
 *
 * 字段固定：
 *   slug     string   作品标识
 *   title    string   标题
 *   summary  string   一句话简介
 *   url      string   完整链接（Toy 托管页形如 https://www.bilibili.com/toy/<slug>/index.html）
 *   poster   string   封面图（相对本站根路径或完整 URL；页面从任何目录打开都能取到）
 *   channel  string   "toy"（B站 Toy 托管）或 "self"（本站自带页）
 *   status   string   "live"（今天就能访问）/ "pending"（等前置，如内测资格）
 *
 * Toy 平台的托管规则（2026-10 实测）：作品页跑在 www.bilibili.com/toy/<slug>/ 子路径下，
 * slug 发布后不可改；资源必须走相对路径，路由用 hash 模式，否则白屏 / 404。
 */
window.INFIVERSE_TOYS = [];
