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
  /* ★ 一条引文如果不在主观测点那棵树上成立，它就必须说出自己读自哪棵树。
     只在前者与后者不同时才显示 —— 全都标一遍等于没标。 */
  function readAtNote(D, e) {
    if (!e.readAt || e.readAt === D.ref) return null;
    return el('p', 'source__readat',
      '这一条的引文读自 ' + String(e.readAt).slice(0, 7) + '（不是台账的主观测点 ' + String(D.ref).slice(0, 7) +
      '）—— 因为那句话在那棵树上不存在，或已被改写。');
  }

  function sourceBox(D, e) {
    var box = el('div', 'source');
    var head = el('p', 'source__head');
    head.appendChild(el('span', 'source__label', '出处'));
    head.appendChild(el('code', 'source__where', e.source.file + ' §' + e.source.section));
    box.appendChild(head);
    box.appendChild(el('blockquote', 'source__quote', e.source.quote));
    /* ★★ 出处节（`ruledIn`）与引文（`source`）是两件事：
       引文说【这句话在哪、逐字如何】，出处节说【这条裁定被记在哪一节】。
       两者可以指向不同的文件（例：裁定记在 `docs/AUDIT.md`，而引文在 `docs/HANDOFF_INFIVERSE.md`）。 */
    if (e.ruledIn) {
      var ri = el('p', 'source__ruledin');
      ri.appendChild(el('span', 'source__label', '裁定记在'));
      ri.appendChild(el('code', 'source__where', e.ruledIn.file + ' §' + e.ruledIn.section));
      if (e.ruledIn.seenOn) {
        ri.appendChild(el('span', 'muted', '（节名在 ' + String(e.ruledIn.seenOn).slice(0, 7) + ' 上看到；★ 只记节名、不记行号。）'));
      }
      box.appendChild(ri);
    }
    /* ★ 【裁定记在】与【登记在】是两件事：前者是这条裁定自己的小节，
       后者是另一棵树上把它归档的那一节。★ 把一条裁定指到它的登记处，就是把它指到一个不是它的地方。 */
    if (e.registeredIn) {
      var rg = el('p', 'source__registeredin');
      rg.appendChild(el('span', 'source__label', '登记在'));
      rg.appendChild(el('code', 'source__where', e.registeredIn.file + ' §' + e.registeredIn.section));
      if (e.registeredIn.seenOn) {
        rg.appendChild(el('span', 'muted', '（读自 ' + String(e.registeredIn.seenOn).slice(0, 7) + '）'));
      }
      box.appendChild(rg);
    }
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

  /* ★★ 第二维度：「做到没有」。
     台账原来的 `ruled` 徽章同时声称了「裁过了」与（读者会默认的）「做到了」——
     而仓库自己把「有没有代码」与「有没有测试盯着」写成两个问题，并明说这一层与三分清单正交、不许混用。
     这一块把被压掉的那一维接回来：逐条的 `built` 优先；否则用 `builtAxes.byFile` 里的**文档级**声明；
     两处都没有 ⇒ 「docs 未记录做到没有」（★ 那是一个零，而零要写出来，不能留白）。 */
  var BUILT_LABEL = {
    done: '做到了', partial: '部分做到', none: '尚未做到', unknown: 'docs 未记录做到没有',
  };
  function builtInfo(D, e) {
    if (e.built) return { built: e.built, scope: e.builtScope || 'section', src: e.builtSource || null, why: null };
    var byFile = (D.builtAxes && D.builtAxes.byFile) || {};
    var b = byFile[e.source.file];
    if (b) return { built: b.built, scope: b.scope || 'doc', src: b.source || null, why: b.why || null };
    return { built: 'unknown', scope: null, src: null, why: null };
  }

  function decisionCard(D, e) {
    var card = el('article', 'decision decision--' + e.status);
    card.id = e.id;

    var head = el('div', 'decision__head');
    head.appendChild(el('code', 'decision__id', e.id));
    head.appendChild(statusBadge(D, e.status));
    /* ★ 用逐条的 ruledBy，不用 humanRuled.ids —— 后者是【那套做法的出处】，不是花名册。
       一旦把它当花名册用，新裁的条目就会静默地不带这个标记，而页面上看不出少了什么。 */
    /* ★★ `ruledBy` 是【值】不是布尔：文件里现在有两条路 ——
       `human`（2026-10 在 `ask_user_question` 上选的）与 `earlier`（更早的一次裁定）。
       ★ 一个徽章只能压掉一个维度，所以按值渲染，而不是「是/不是人类选的」。 */
    if (e.ruledBy) {
      /* ★★ 归属路线表从数据读（`D.ruledByRoutes`）。
         它以前是写死在这个函数体里的一个对象字面量（只有两条），
         而语料里逐字存在第三条（「（协调者裁定）」，`docs/STATUS.md` §10.4）。
         ★ 判据：**一个写死在渲染函数里的对照表，它的集合无从数据里被量** ——
         而它的缺口不会报错，只会在页面上把那一条降级成一个灰色小块。 */
      var route = null;
      (D.ruledByRoutes || []).forEach(function (r) { if (r.key === e.ruledBy) route = r; });
      var label = route ? route.label : '谁裁的：' + e.ruledBy;
      var cls = route ? route.badge : 'badge--muted';
      var badge = el('span', 'badge ' + cls, label);
      if (route && route.where) badge.title = '出处：' + route.where;
      head.appendChild(badge);
    }
    card.appendChild(head);

    card.appendChild(el('h3', 'decision__title', e.title));
    card.appendChild(field('决定了什么', e.decided, 'decided'));
    card.appendChild(e.alternative ? field('另一条路', e.alternative, 'alt') : unrecorded(D));
    if (e.whyNot) card.appendChild(field('为什么没走', e.whyNot, 'why'));
    if (e.overturn) card.appendChild(field('要推翻它需要什么', e.overturn, 'overturn'));
    if (e.flag) card.appendChild(field('注意', e.flag, 'flag'));
    var bi = builtInfo(D, e);
    var bl = el('div', 'field field--built');
    bl.appendChild(el('span', 'field__label', '做到没有'));
    var bv = el('span', 'field__value');
    bv.appendChild(el('span', 'badge badge--built badge--built-' + bi.built, BUILT_LABEL[bi.built]));
    if (bi.scope === 'doc') bv.appendChild(el('span', 'muted', '（文档级声明，不是这一节自己的读数）'));
    if (bi.src) bv.appendChild(el('span', 'muted', '出处：' + bi.src.file + ' §' + bi.src.section));
    bl.appendChild(bv);
    card.appendChild(bl);
    card.appendChild(sourceBox(D, e));
    var ra = readAtNote(D, e);
    if (ra) card.appendChild(ra);
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
      var human = D.entries.filter(function (e) { return e.ruledBy === 'human'; }).length;
      summary.appendChild(el('p', null,
        '共 ' + D.entries.length + ' 条：' + Object.keys(n).map(function (k) {
          return (D.statuses[k] ? D.statuses[k].label : k) + ' ' + n[k];
        }).join(' · ')));
      summary.appendChild(el('p', 'muted',
        '其中 ' + human + ' 条是 2026-10 人类在 ask_user_question 上逐条选定的 —— ' +
        '「已裁」不是一种，是两种：这四条是人当场选的，另有几条同样标着【已裁】、' +
        '出处却是更早的裁定。页面上这两者分开标，否则「谁裁的」就消失了。'));
    }

    /* ★ 被压掉的那一维：把仓库自己那句「两层正交、不许混用」和那处撞车原句放在列表之前 */
    var axesBox = document.getElementById('decisions-axes');
    if (axesBox && D.builtAxes) {
      axesBox.appendChild(el('p', null,
        '★ 「裁过了」与「做到了」是两个正交的维度 —— 这个仓库自己这么说：'));
      axesBox.appendChild(el('blockquote', 'verbatim', D.builtAxes.axes.source.quote));
      axesBox.appendChild(el('p', 'muted',
        D.builtAxes.axes.source.file + ' §' + D.builtAxes.axes.source.section + ' · ' + D.builtAxes.axes.why));
      axesBox.appendChild(el('p', null,
        '★ 而有一处原句字面上就同时写着这两件事：'));
      axesBox.appendChild(el('blockquote', 'verbatim', D.builtAxes.collision.source.quote));
      axesBox.appendChild(el('p', 'muted', D.builtAxes.collision.why));
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
      var meta = doc.file + ' · ' + doc.lines + ' 行 · 引用规矩：' + doc.cite;
      if (doc.ref && doc.ref !== S.ref) {
        meta += ' · ★ 元信息读自 ' + doc.ref.slice(0, 7) +
                '（不是本页的主观测点 ' + S.ref.slice(0, 7) + '）';
      }
      box.appendChild(el('p', 'spec-doc__meta', meta));

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

    /* ★★ 「未归组」那一档 —— 一个按组处理行的渲染器，它会把不属于任何组的行《静默丢掉》，
       而丢掉的方式是「页面上少了一块」——它不会报错。所以它必须被单独印出来。
       ★ 判据：**一个只会「数对不上」的对照，它的修法可以是补一个组、也可以是把那一行藏起来 —— 两者在输出里长得一样。** */
    if (D) {
      var known = {};
      S.docs.forEach(function (d) { known[d.file] = true; });
      var orphans = D.entries.filter(function (e) { return !known[e.source.file]; });
      /* ★ 它不叫 `spec-doc` —— 它不是一份文档，它是「不属于任何文档的那些行」。
         ★ 判据：**把一个残余类放进被它残余掉的那个类里，那个类的计数就会多一——而它看起来像「多了一份文档」。** */
      var ob = el('article', 'spec-orphans');
      ob.id = 'spec-orphan';
      ob.appendChild(el('h3', 'spec-doc__title', '未归组的裁定（' + orphans.length + ' 条）'));
      ob.appendChild(el('p', 'spec-doc__meta',
        '★ 这一档列的是「`source.file` 不在上面那份文档清单里」的条目。' +
        '它为 0 时才说明上面那份清单是完整的。'));
      if (orphans.length) {
        ob.appendChild(el('p', 'muted',
          '★ 它们之前会被静默丢掉 —— “' +
          orphans.map(function (e) { return e.source.file; }).filter(function (v, i, a) { return a.indexOf(v) === i; }).join('、') +
          '” 这几份文档不在 `website/data/spec.js` 的 `docs` 里。'));
      }
      orphans.forEach(function (e) { ob.appendChild(decisionCard(D, e)); });
      host.appendChild(ob);
    }

    var refBox = document.getElementById('spec-ref');
    if (refBox) {
      refBox.appendChild(el('p', null,
        '文档元信息钉在 ref ' + S.ref + ' 上：' + S.docs.length +
        ' 份文档的标题、行数、文件头逐字，全部由 website/scratch/check_citations.js 核对。'));
      /* ★★ 这张清单的名字是【手抄对照清单】，不是「规范页覆盖的文档」——
         它没有任何一条能被违反的规则在管（见 website/data/spec.js 文件头的说明）。
         它先前叫「规范页元信息」，改名是因为那个名字在暗示一份**覆盖率**，而它只是一张手抄清单。 */
      refBox.appendChild(el('p', 'muted', '★ 手抄对照清单：它不是任何规则产生的 —— 今天没有一条能被违反的规则在管它。'));
      refBox.appendChild(el('p', 'muted',
        '★ 行数是一个会过期的数：它随下一笔提交变假 —— 而这一条会说话（守卫会红）。'));
    }
  }


  /* ══════════════════ 渐进教程 ══════════════════
   * 渲染源 = data/tutorial.js（window.INFIVERSE_TUTORIAL），**生成物**，
   * 每一步的 stdout / stderr / 退出码都是真跑出来的。
   *
   * ★ 本渲染器的三条纪律（前两条是全站通用的、第三条是本页特有的）：
   *   1. **一个零也要印出来。** stdout 为空、stderr 为空、rc=0 —— 三个都是读数，
   *      省略任何一个，读者就分不清「它是空的」与「我没印它」。
   *   2. **cwd 与 flags 逐条印出来**，因为它们**改变读数**：同一个脚本换个 cwd
   *      stdout 就不一样（见 t01c 与 t01b）。不印 cwd 的教程，读者复现不出来。
   *   3. **`ok=false` 的步骤不许悄悄消失** —— 它要么被印成一个失败的步骤，
   *      要么把它的条数印在顶上。悄悄丢掉正是「跑通了」与「在演示」混淆的地方。
   */
  function renderTutorial() {
    var host = document.getElementById('tutorial-list');
    if (!host) return;
    var T = window.INFIVERSE_TUTORIAL;
    if (!T) {
      host.appendChild(el('p', 'muted',
        'data/tutorial.js 没有加载 —— 这一页是按它渲染的，没有它这一页是空的。'));
      return;
    }

    var meta = document.getElementById('tutorial-meta');
    if (meta) {
      var okN = T.steps.filter(function (s) { return s.ok; }).length;
      meta.appendChild(el('p', null,
        '产物钉在 ref ' + T.ref + '（' + T.ref_date + '）；引擎 ' + T.engine +
        '；' + T.steps.length + ' 步，其中跑通的 ' + okN + ' 步。'));
      meta.appendChild(el('p', 'muted',
        '★ 这一页的每一格输出都由 ' + T.generated_by + ' 真跑出来，不要手改 data/tutorial.js。'));
      /* ★ 一个「跑不通的步骤数」必须被印出来，哪怕它是 0 —— 它 0 的时候说明上面那条
         「跑通的 N 步」等于全部；它非 0 的时候读者才知道少的是哪几步。 */
      meta.appendChild(el('p', 'muted',
        '★ 没通过生成器 ok 判定的步骤 = ' + T.failed.length +
        (T.failed.length ? '（' + T.failed.map(function (f) { return f.id; }).join('、') + '）' : '') +
        ' —— 它们**不出现在下面**，所以这个数必须印在顶上。'));
      if (T.defaults) {
        meta.appendChild(el('p', 'muted', '★ 默认参数 ' +
          T.defaults.flags.join(' ') + ' —— ' + T.defaults.why));
      }
    }

    T.steps.forEach(function (s, i) {
      var card = el('article', 'tut-step');
      card.id = 'step-' + s.id;

      var h = el('h3', 'tut-step__title',
        '第 ' + (i + 1) + ' 步 · ' + s.id + ' —— ' + s.teaches);
      card.appendChild(h);

      /* ---- 怎么跑：命令 + cwd + flags，三个都印 ---- */
      var how = el('dl', 'tut-how');
      how.appendChild(el('dt', null, '命令'));
      how.appendChild(el('dd', null, s.cmd));
      how.appendChild(el('dt', null, 'cwd'));
      how.appendChild(el('dd', null, s.cwdNote + '（声明值 ' + JSON.stringify(s.cwd) + '）'));
      how.appendChild(el('dt', null, '参数'));
      how.appendChild(el('dd', null, s.flags.length ? s.flags.join(' ') : '（无）'));
      card.appendChild(how);

      /* ---- 源码 ---- */
      if (s.src !== null) {
        var srcBox = el('div', 'tut-block');
        srcBox.appendChild(el('p', 'tut-block__label', '脚本（逐字）'));
        var preS = el('pre', 'tut-pre');
        preS.textContent = s.src;
        srcBox.appendChild(preS);
        card.appendChild(srcBox);
      }

      /* ---- 输出：stdout / stderr / 退出码，一个都不省 ---- */
      var outBox = el('div', 'tut-block');
      outBox.appendChild(el('p', 'tut-block__label', '真跑出来的输出'));

      outBox.appendChild(el('p', 'tut-stream', 'stdout'));
      var preO = el('pre', 'tut-pre');
      /* ★ 空串印成「（空）」，不印成一块空白 —— 空白读起来像「这里没有东西」，
         而「（空）」读起来像「这个流是空的」，后者才是读数。 */
      preO.textContent = s.stdout === '' ? '（空）' : s.stdout;
      if (s.stdout === '') preO.className += ' tut-pre--empty';
      outBox.appendChild(preO);

      outBox.appendChild(el('p', 'tut-stream', 'stderr'));
      var preE = el('pre', 'tut-pre');
      preE.textContent = s.stderr === '' ? '（空）' : s.stderr;
      if (s.stderr === '') preE.className += ' tut-pre--empty';
      outBox.appendChild(preE);

      outBox.appendChild(el('p', 'tut-stream', '退出码'));
      var rc = el('p', 'tut-rc' + (s.rc === 0 ? ' tut-rc--zero' : ' tut-rc--nonzero'));
      rc.textContent = (s.rc === null ? '（没拿到）' : String(s.rc)) +
        (s.rc === 0 ? '　—— 这一步不报错' : '　—— 这一步**报错**，而这就是它要教的');
      outBox.appendChild(rc);
      card.appendChild(outBox);

      /* ---- 稳定化：逐条印规则，空也印 ---- */
      var stab = el('p', 'muted',
        '稳定化规则 = ' + (s.stabilized.length
          ? s.stabilized.map(function (h) { return h.id + '×' + h.n; }).join('、')
          : '（一个都没施加 ⇒ 上面的 stdout / stderr 字字原样）'));
      card.appendChild(stab);

      if (s.note) card.appendChild(el('p', 'callout callout--tight', s.note));
      card.appendChild(el('p', 'muted',
        '★ 「跑通了」与「演示了它说要演示的那件事」是两个判据 —— 本页每一步的 ' +
        'stdout / stderr / cwd / 参数 四元组在全表里唯一，重复的那一对会被生成器点名。'));
      host.appendChild(card);
    });
  }


  /* ============================================================
     渐进教程（website/tutorial/，渲染源 = data/tutorial.js）
     ★ 本函数只做一件事：把 data/tutorial.js 里已经**真的跑出来**的四个读数
       原样搬进 DOM。它不解释语法、不推断结果、不改写任何一行输出。
     ★★ 「（空）」是硬规则：stdout 为空、stderr 为空都必须印出来。
       一个被省略的零与一个真的零在页面上长得一样，而它们不是一回事。
     ============================================================ */
  function tutBox(label, text, extraClass) {
    var box = el('div', 'tut-io__box');
    var lab = el('div', 'tut-io__label');
    lab.appendChild(el('span', null, label));
    if (extraClass) lab.appendChild(el('span', null, extraClass));
    box.appendChild(lab);
    var body = el('pre', 'tut-io__body' + (text ? (extraClass === 'stderr' ? ' tut-io__body--err' : '') : ' tut-io__body--empty'),
      text === '' ? '（空）' : text);
    box.appendChild(body);
    return box;
  }

  function renderTutorial() {
    var host = document.getElementById('tutorial-list');
    if (!host) return;
    var T = window.INFIVERSE_TUTORIAL;

    var meta = document.getElementById('tutorial-meta');
    if (meta) {
      if (!T) {
        meta.appendChild(el('p', 'muted', '★ data/tutorial.js 没有加载 —— 这一页的每一步都从它来，没有它就一步也没有。'));
        return;
      }
      meta.appendChild(el('p', null,
        '共 ' + T.steps.length + ' 步，全部在本机真跑过：ok = ' + (T.steps.length - T.failed.length) +
        '，不 ok = ' + T.failed.length + '。'));
      meta.appendChild(el('p', 'tut-meta',
        '读数的树 = ' + T.ref + '（' + T.ref_date + '）　引擎 = ' + T.engine +
        '　生成器 = ' + T.generated_by));
      meta.appendChild(el('p', 'tut-meta',
        '★ 下面每一行的 stdout / stderr / rc 都是**生成器的原样记录**，不是抄来的、也不是手写的。' +
        '重构引擎之后跑一遍生成器，与这里对不上就是它红了。'));
    }

    var frag = document.createDocumentFragment();
    T.steps.forEach(function (s, i) {
      var art = el('article', 'tut-step');
      art.id = 'step-' + s.id;

      var head = el('div', 'tut-step__head');
      head.appendChild(el('span', 'tut-step__id', s.id));
      head.appendChild(el('span', 'tut-step__teach', s.teaches));
      head.appendChild(el('span', 'badge ' + (s.rc === 0 ? 'badge--built-done' : 'badge--built-partial'),
        'rc = ' + s.rc));
      art.appendChild(head);

      /* cwd 是这一步的一个**变量**，不是装饰：它一变，输出就变（见 t01 / t01c / t01b）。 */
      var where = el('p', 'tut-meta');
      where.appendChild(el('span', null, '在哪儿跑：' + s.cwd));
      where.appendChild(el('span', null, '　' + s.cwdNote));
      art.appendChild(where);

      art.appendChild(el('pre', 'tut-cmd', s.cmd));

      /* ★ 键名叫 src，值是**源码正文**（生成器把 SRC[step.src] 塞进来的）；
         argv 类的步骤没有源码，这里就什么都不画。 */
      if (s.src) art.appendChild(el('pre', 'tut-source', s.src));

      var io = el('div', 'tut-io');
      io.appendChild(tutBox('stdout', s.stdout, 'stdout'));
      io.appendChild(tutBox('stderr', s.stderr, 'stderr'));
      art.appendChild(io);

      if (s.note) art.appendChild(el('p', 'tut-note', s.note));
      if (s.stabilized && s.stabilized.length) {
        art.appendChild(el('p', 'tut-stab', '★ 已经消掉的不确定性：' + s.stabilized.join('、')));
      } else {
        art.appendChild(el('p', 'tut-stab', '★ 已经消掉的不确定性：（无）—— stdout / stderr 一个字都没动过。'));
      }
      frag.appendChild(art);
    });
    host.appendChild(frag);

    var probe = document.getElementById('tutorial-probes');
    if (probe) {
      var d = T.diffProbe || {};
      probe.appendChild(el('p', null, '★★ 两条反例是否真的与它们的对照不同：' + (d.allDifferent ? '成立' : '★ 不成立')));
      (d.pairs || []).forEach(function (p) {
        probe.appendChild(el('p', 'tut-meta',
          p.id + '　stdout 相同 = ' + p.stdoutSame + '　stderr 相同 = ' + p.stderrSame +
          '　cwd 相同 = ' + p.cwdSame + '　flags 相同 = ' + p.flagsSame));
      });
      probe.appendChild(el('p', 'tut-meta', d.why || ''));
      var q = T.dupProbe || {};
      probe.appendChild(el('p', null, '★ 四元组 (stdout, stderr, cwd, flags) 全表唯一：' + (q.clean ? '是' : '★ 否 ' + JSON.stringify(q.duplicates))));
      probe.appendChild(el('p', 'tut-meta', q.why || ''));
      probe.appendChild(el('p', 'muted', '★ 这两条探针是在第一版生成器**印出两笔一模一样的读数**之后补上的：' +
        '一个只检查「命令跑通了」的生成器，对一个什么都没演示的步骤永远是绿的。'));
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
    renderTutorial();
    renderTutorial();
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
