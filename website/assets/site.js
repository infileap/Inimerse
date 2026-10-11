/* 站点渲染 —— 经典脚本，无模块、无 fetch、无依赖。
   这样 file:// 双击打开、静态托管、整包上传到 B站 Toy 三种场景行为一致。 */
(function () {
  'use strict';

  var BVID_RE = /^BV[0-9A-Za-z]{8,12}$/;

  /* ---------- 站点根：数据里的路径是「相对站点根」写的 ----------
     data/videos.js 里的 cover 写成 "assets/covers/x.jpg"。一份数据被首页和各子页共用，
     所以它没法写成 "../assets/..."（首页要的是没有 ../ 的那个）。
     而浏览器解析相对路径是按**页面目录**来的：从 /videos/ 打开会去找 /videos/assets/...
     ⇒ 404；整包传到 B站 Toy 的 /toy/<slug>/ 下同理，更糟。
     所以这里从本脚本自己的 URL 反推站点根 —— 页面嵌多深都对，且不需要每页写一个前缀。
     取不到时返回 ''（退回改动前的行为，不制造新故障）。 */
  function siteRoot() {
    try {
      var s = document.currentScript;
      if (!s || !s.src) {
        var all = document.getElementsByTagName('script');
        for (var i = all.length - 1; i >= 0; i--) {
          var u = all[i].src || '';
          if (/\/assets\/site\.js(\?|$)/.test(u)) { s = all[i]; break; }
        }
      }
      if (s && s.src) return s.src.replace(/assets\/site\.js(\?.*)?$/, '');
    } catch (e) { /* 取不到就退回旧行为 */ }
    return '';
  }
  var SITE_BASE = siteRoot();

  /** 站点根相对路径 → 浏览器真能取到的地址；外链与绝对路径原样返回 */
  function asset(p) {
    if (!p || /^[a-z][a-z0-9+.-]*:/i.test(p) || p.charAt(0) === '/') return p;
    return SITE_BASE + p;
  }

  function el(tag, cls, text) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  }

  function isPublished(bvid) {
    return typeof bvid === 'string' && BVID_RE.test(bvid.trim());
  }

  /* ---------- 视频卡 ---------- */
  function videoCard(rec) {
    var portrait = rec.orientation === 'portrait';
    var card = el('article', 'video' + (rec.featured ? ' video--featured' : ''));

    var coverHref = isPublished(rec.bvid)
      ? 'https://www.bilibili.com/video/' + rec.bvid.trim() + '/'
      : null;

    var cover = el(coverHref ? 'a' : 'div',
      'video__cover' + (portrait ? ' video__cover--portrait' : ''));
    if (coverHref) {
      cover.href = coverHref;
      cover.target = '_blank';
      cover.rel = 'noopener';
    }

    if (rec.cover) {
      var img = document.createElement('img');
      img.src = asset(rec.cover);
      img.alt = rec.title || '';
      img.loading = 'lazy';
      cover.appendChild(img);
    } else {
      cover.appendChild(el('span', 'video__placeholder', '封面待交付'));
    }
    if (rec.duration) cover.appendChild(el('span', 'video__dur', rec.duration));
    card.appendChild(cover);

    var body = el('div', 'video__body');
    body.appendChild(el('h3', 'video__title', rec.title || '(无标题)'));
    if (rec.summary) body.appendChild(el('p', 'video__desc', rec.summary));

    var meta = el('p', 'video__meta');
    var parts = [];
    if (rec.resolution) parts.push(rec.resolution);
    parts.push(isPublished(rec.bvid) ? 'B站 ' + rec.bvid.trim() : 'B站 未投稿');
    if (rec.date) parts.push(rec.date);
    if (rec.ref) parts.push('ref ' + String(rec.ref).slice(0, 7));
    parts.forEach(function (p) { meta.appendChild(el('span', null, p)); });
    body.appendChild(meta);

    var actions = el('div', 'video__actions');
    if (coverHref) {
      var watch = el('a', 'btn btn--primary', '在 B站 观看');
      watch.href = coverHref; watch.target = '_blank'; watch.rel = 'noopener';
      actions.appendChild(watch);

      var embed = el('button', 'btn btn--ghost', '在站内播放');
      embed.type = 'button';
      embed.addEventListener('click', function () {
        var iframe = document.createElement('iframe');
        iframe.className = 'embed';
        iframe.src = 'https://player.bilibili.com/player.html?bvid=' +
          encodeURIComponent(rec.bvid.trim()) + '&page=1&autoplay=0';
        iframe.allowFullscreen = true;
        iframe.setAttribute('scrolling', 'no');
        iframe.setAttribute('frameborder', '0');
        iframe.title = rec.title || 'B站播放器';
        cover.replaceWith(iframe);
        actions.remove();
      });
      actions.appendChild(embed);
    } else {
      actions.appendChild(el('span', 'badge badge--partial', '待投稿后补 BV 号'));
    }
    body.appendChild(actions);

    card.appendChild(body);
    return card;
  }

  function renderVideos() {
    var host = document.getElementById('video-list');
    if (!host) return;
    var list = window.INFIVERSE_VIDEOS || [];
    host.textContent = '';
    if (!list.length) {
      var empty = el('div', 'empty');
      empty.appendChild(el('p', null, '还没有视频记录。'));
      empty.appendChild(el('p', null,
        '记录格式见 data/videos.js 顶部；没有记录的视频不在这里显示。'));
      host.appendChild(empty);
      return;
    }
    list.forEach(function (rec) { host.appendChild(videoCard(rec)); });
  }

  /* ---------- Toy / 在线作品卡 ---------- */
  function renderToys() {
    var host = document.getElementById('toy-list');
    if (!host) return;
    var list = window.INFIVERSE_TOYS || [];
    host.textContent = '';
    if (!list.length) {
      var empty = el('div', 'empty');
      empty.appendChild(el('p', null, '还没有已发布的作品。'));
      empty.appendChild(el('p', null,
        '发布权限需要 B站 Toy 内测资格（当前为邀请制）；拿到资格后用官方 toy CLI 提交。'));
      host.appendChild(empty);
      return;
    }
    list.forEach(function (t) {
      var card = el('a', 'card');
      card.href = t.url || '#';
      if (t.url && t.channel === 'toy') { card.target = '_blank'; card.rel = 'noopener'; }
      if (t.poster) {                       // 数据里的路径同样是「相对站点根」，走 asset()
        var poster = el('img', 'card__poster');
        poster.src = asset(t.poster);
        poster.alt = t.title || t.slug || '';
        card.appendChild(poster);
      }
      card.appendChild(el('h3', null, t.title || t.slug || '(无标题)'));
      if (t.summary) card.appendChild(el('p', null, t.summary));
      var badges = el('div', 'badges');
      badges.appendChild(el('span',
        'badge ' + (t.status === 'live' ? 'badge--live' : 'badge--partial'),
        t.status === 'live' ? '今天可玩' : '等前置：内测资格'));
      if (t.channel === 'toy') badges.appendChild(el('span', 'badge badge--muted', 'B站 Toy 托管'));
      card.appendChild(badges);
      host.appendChild(card);
    });
  }

  /* ==================== 裁定台账 / 规范页 ====================
     两页读**同一份** data/decisions.js —— 规范页按「文档」分组看它，
     台账页按「一条一条裁定」看它。同一件事写两遍，下一轮就会有两份不一致。 */

  /** 一条字段：标签 + 正文。标签写死在函数里，正文全部来自数据。 */
  function field(label, text, cls) {
    var box = el('div', 'field' + (cls ? ' field--' + cls : ''));
    box.appendChild(el('span', 'field__label', label));
    box.appendChild(el('p', 'field__text', text));
    return box;
  }

  function statusBadge(D, key) {
    var s = (D.statuses && D.statuses[key]) || { label: key, cls: 'badge--muted' };
    return el('span', 'badge ' + (s.cls || 'badge--muted'), s.label || key);
  }

  /** 出处：文件 + 节号（不写行号 —— 行号会失效，节号不会）+ 逐字引文 */
  function sourceBox(D, e) {
    var box = el('div', 'source');
    var head = el('p', 'source__head');
    head.appendChild(el('span', 'source__label', '出处'));
    head.appendChild(el('code', 'source__where', e.source.file + ' §' + e.source.section));
    box.appendChild(head);
    box.appendChild(el('blockquote', 'source__quote', e.source.quote));
    /* ★ 纪律 3：引 PLAN_V07 的数字，必须同时显示它 §0.3 那句 */
    if (e.source.file === 'docs/PLAN_V07.md' && D.planV07Note) {
      box.appendChild(el('p', 'source__warn',
        '⚠ ' + D.planV07Note.quote + '（' + D.planV07Note.file + ' §' + D.planV07Note.section + '）'));
    }
    return box;
  }

  /** 「docs 未记录备选」—— 一个零必须被归因，否则它和「我没找到」长得一样 */
  function unrecorded(D) {
    var box = el('div', 'field field--zero');
    box.appendChild(el('span', 'field__label', '另一条路'));
    box.appendChild(el('p', 'field__text', 'docs 未记录备选。'));
    var a = D.altSearch;
    if (a) {
      box.appendChild(el('p', 'field__attribution',
        '这是归因，不是结论：查过 ' + a.files.join('、') +
        '；筛法 ' + a.method + '；命中 ' + Object.keys(a.counts).map(function (k) {
          return k.replace(/^docs\//, '') + ' ' + a.counts[k].ruled + '/' + a.counts[k].alt;
        }).join('、') +
        '（前一个数是「裁定」的出现次数，后一个数是「备选/否决/放弃/另一条/没有走/不选」的出现次数）。'));
    }
    return box;
  }

  function decisionCard(D, e) {
    var card = el('article', 'decision decision--' + e.status);
    card.id = e.id;

    var head = el('div', 'decision__head');
    head.appendChild(el('code', 'decision__id', e.id));
    head.appendChild(statusBadge(D, e.status));
    if (D.humanRuled && D.humanRuled.ids.indexOf(e.id) !== -1) {
      head.appendChild(el('span', 'badge badge--human', '人类在 ask_user_question 上选的'));
    }
    card.appendChild(head);

    card.appendChild(el('h3', 'decision__title', e.title));
    card.appendChild(field('决定了什么', e.decided, 'decided'));
    card.appendChild(e.alternative ? field('另一条路', e.alternative, 'alt') : unrecorded(D));
    if (e.whyNot) card.appendChild(field('为什么没走', e.whyNot, 'why'));
    if (e.overturn) card.appendChild(field('要推翻它需要什么', e.overturn, 'overturn'));
    if (e.flag) card.appendChild(field('注意', e.flag, 'flag'));
    card.appendChild(sourceBox(D, e));
    return card;
  }

  function renderDecisions() {
    var D = window.INFIVERSE_DECISIONS;
    if (!D) return;

    /* 图例：状态词表来自数据，不在页面里手写 */
    var legend = document.getElementById('decisions-legend');
    if (legend) {
      Object.keys(D.statuses).forEach(function (k) {
        var item = el('div', 'legend__item');
        item.appendChild(statusBadge(D, k));
        item.appendChild(el('span', 'legend__note', D.statuses[k].note || ''));
        legend.appendChild(item);
      });
    }

    var summary = document.getElementById('decisions-summary');
    if (summary) {
      var n = {};
      D.entries.forEach(function (e) { n[e.status] = (n[e.status] || 0) + 1; });
      var human = D.humanRuled ? D.humanRuled.ids.length : 0;
      summary.appendChild(el('p', null,
        '共 ' + D.entries.length + ' 条：' + Object.keys(n).map(function (k) {
          return (D.statuses[k] ? D.statuses[k].label : k) + ' ' + n[k];
        }).join(' · ')));
      summary.appendChild(el('p', 'muted',
        '其中 ' + human + ' 条是 2026-10 人类在 ask_user_question 上逐条选定的 —— ' +
        '「已裁」不是一种，是两种：这四条是人当场选的，另有几条同样标着【已裁】、' +
        '出处却是更早的裁定。页面上这两者分开标，否则「谁裁的」就消失了。'));
    }

    /* 筛选：按状态。★ 不筛「有没有备选」—— 那会让「docs 未记录备选」被藏起来。 */
    var bar = document.getElementById('decision-filter');
    var host = document.getElementById('decision-list');
    function paint(only) {
      if (!host) return;
      host.innerHTML = '';
      D.entries.filter(function (e) { return !only || e.status === only; })
        .forEach(function (e) { host.appendChild(decisionCard(D, e)); });
    }
    if (bar) {
      var all = el('button', 'filter filter--on', '全部 ' + D.entries.length);
      all.addEventListener('click', function () { pick(null, all); });
      bar.appendChild(all);
      Object.keys(D.statuses).forEach(function (k) {
        var c = D.entries.filter(function (e) { return e.status === k; }).length;
        if (!c) return;
        var b = el('button', 'filter', (D.statuses[k].label) + ' ' + c);
        b.addEventListener('click', function () { pick(k, b); });
        bar.appendChild(b);
      });
    }
    function pick(only, btn) {
      Array.prototype.forEach.call(bar.children, function (x) { x.className = 'filter'; });
      btn.className = 'filter filter--on';
      paint(only);
    }
    paint(null);

    var refBox = document.getElementById('decisions-ref');
    if (refBox) {
      refBox.appendChild(el('p', null,
        '这一页的每条引文都钉在 ref ' + D.ref + ' 上（' + D.entries.length +
        ' 条引文 / ' + D.entries.length + ' 个节号）。改一个字、或写一个不存在的节号，' +
        'website/scratch/check_citations.js 必须红。'));
      refBox.appendChild(el('p', 'muted',
        '出处写「文件 + 节号」而不是行号：这几份文档自己逐字要求引节号（行号会随下一笔提交失效）。'));
    }
  }

  function renderSpec() {
    var S = window.INFIVERSE_SPEC;
    var D = window.INFIVERSE_DECISIONS;
    var host = document.getElementById('spec-list');
    if (!S || !host) return;

    S.docs.forEach(function (doc) {
      var box = el('article', 'spec-doc');
      box.id = doc.file.replace(/[^A-Za-z0-9]+/g, '-');

      /* 数据里的 title 是**逐字**（带 `#`，受守卫钉住）；显示时去掉那个 Markdown 标记，
         否则页面上会出现一个字面的 `#`。 */
      box.appendChild(el('h3', 'spec-doc__title', doc.title.replace(/^#+\s*/, '')));
      box.appendChild(el('p', 'spec-doc__meta',
        doc.file + ' · ' + doc.lines + ' 行 · 引用规矩：' + doc.cite));

      box.appendChild(field('本站概括（不是原文）', doc.role, 'mine'));

      var vq = el('div', 'field field--verbatim');
      vq.appendChild(el('span', 'field__label', '文件头逐字'));
      var ul = el('ul', 'verbatim');
      doc.headers.forEach(function (h) { ul.appendChild(el('li', null, h.replace(/^>\s?/, ''))); });
      vq.appendChild(ul);
      box.appendChild(vq);

      if (D) {
        var mine = D.entries.filter(function (e) { return e.source.file === doc.file; });
        var head = el('p', 'spec-doc__count',
          mine.length ? '挂在这一份下面的裁定：' + mine.length + ' 条'
                      : '今天没有裁定挂在这一份下面（它的裁定还没进台账）');
        box.appendChild(head);
        mine.forEach(function (e) { box.appendChild(decisionCard(D, e)); });
      }
      host.appendChild(box);
    });

    var refBox = document.getElementById('spec-ref');
    if (refBox) {
      refBox.appendChild(el('p', null,
        '文档元信息钉在 ref ' + S.ref + ' 上：' + S.docs.length +
        ' 份文档的标题、行数、文件头逐字，全部由 website/scratch/check_citations.js 核对。'));
      refBox.appendChild(el('p', 'muted',
        '★ 行数是一个会过期的数：它随下一笔提交变假 —— 而这一条会说话（守卫会红）。'));
    }
  }

  function ready(fn) {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', fn);
    } else { fn(); }
  }

  ready(function () {
    renderVideos();
    renderToys();
    renderDecisions();
    renderSpec();
    var y = document.getElementById('year');
    if (y) y.textContent = String(new Date().getFullYear());

    /* B站 账号按钮：地址没填就不显示（见 data/site.js 的 bilibiliUrl）。 */
    var site = window.INFIVERSE_SITE || {};
    var bili = document.getElementById('bili-account');
    if (bili && site.bilibiliUrl) {
      bili.href = site.bilibiliUrl;
      bili.hidden = false;
    }
  });
})();
