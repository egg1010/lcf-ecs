/* app.js - 文档主应用 (路由/侧边栏/搜索/渲染/粒子) */

(function () {
  'use strict';

  // === 三层导航: 库接口 → 宏配置 → 模块接口 ===
  var BANDS = [
    { id: 'api',     name: '库接口', name_en: 'Library API' },
    { id: 'config',  name: '宏配置', name_en: 'Macros' },
    { id: 'modules', name: '模块接口', name_en: 'Module API' }
  ];

  var CATEGORIES = [
    { id: 'ecs',           name: 'ECS 核心',    name_en: 'ECS Core',       icon: 'E', band: 'api',     entry: 'ecs/manager.hpp' },
    { id: 'reflection',    name: '反射',        name_en: 'Reflection',     icon: 'R', band: 'api',     entry: 'reflection/manager.hpp' },
    { id: 'serialization', name: '序列化',      name_en: 'Serialization',  icon: 'Z', band: 'api',     entry: 'serialization/manager.hpp' },
    { id: 'thread',        name: '多线程',      name_en: 'Threading',      icon: 'X', band: 'api',     entry: 'thread/manager.hpp' },
    { id: 'config',        name: '宏配置',      name_en: 'Macros',         icon: '#', band: 'config' },
    { id: 'containers',    name: '容器',        name_en: 'Containers',     icon: 'C', band: 'modules' },
    { id: 'allocators',    name: '内存分配器',  name_en: 'Allocators',     icon: 'A', band: 'modules' },
    { id: 'algorithms',    name: '算法',        name_en: 'Algorithms',     icon: 'S', band: 'modules' },
    { id: 'codec',         name: '编解码',      name_en: 'Codec',          icon: 'D', band: 'modules' },
    { id: 'time',          name: '时间',        name_en: 'Time',           icon: 'T', band: 'modules' },
    { id: 'type',          name: '类型',        name_en: 'Types',          icon: 'Y', band: 'modules' },
    { id: 'utf8',          name: 'UTF-8 模块',  name_en: 'UTF-8',          icon: 'U', band: 'modules' },
    { id: 'tools',         name: '通用工具',    name_en: 'Utilities',      icon: 'K', band: 'modules' }
  ];

  // 语言状态: 默认中文, 选择写入 localStorage
  var LANG_KEY = 'lcf-doc-lang';
  var LANG = 'zh';
  try {
    if (localStorage.getItem(LANG_KEY) === 'en') { LANG = 'en'; }
  } catch (e) { /* 隐私模式忽略 */ }

  var UI_TEXT = {
    zh: {
      heroTitle: 'LCF-ECS 接口文档',
      heroSubtitle: 'C++20 · {n} 个模块 · 零外部依赖',
      entrySection: '按模块开始',
      guideSection: '怎么读这份文档',
      guide: [
        ['库接口', '先看模块入口与调用方式', '含 ECS、反射、序列化、多线程'],
        ['宏配置', '需要改默认值或容量时再查', '所有配置在包含头文件前定义'],
        ['模块接口', '按类型、容器、工具逐页查接口', '每页含接口表、示例与常见错误']
      ],
      statTotal: '模块总数', statCategory: '分类', statStandard: '标准', statDeps: '外部依赖',
      searchPlaceholder: '搜索模块或接口... ( 按 / 聚焦 )',
      searchEmpty: '未找到匹配的模块或小节',
      tocTitle: '本页导航', tocCount: '{n} 节',
      copy: '复制', copied: '已复制', copyFailed: '复制失败', copyTitle: '复制代码',
      prev: '上一页', next: '下一页', home: '返回首页',
      headingLevel: '级标题',
      switchTitle: '切换语言',
      enFallback: ''
    },
    en: {
      heroTitle: 'LCF-ECS API Reference',
      heroSubtitle: 'C++20 · {n} modules · No third-party dependencies',
      entrySection: 'Start by module',
      guideSection: 'How to read these docs',
      guide: [
        ['Library API', 'Start with module entries and call flow', 'ECS, reflection, serialization, threading'],
        ['Macros', 'Check only when defaults or capacities change', 'Define before including headers'],
        ['Module API', 'Look up types, containers and utilities', 'Each page has tables, examples and pitfalls']
      ],
      statTotal: 'Modules', statCategory: 'Categories', statStandard: 'Standard', statDeps: 'Dependencies',
      searchPlaceholder: 'Search modules or sections... ( / to focus )',
      searchEmpty: 'No matching module or section',
      tocTitle: 'On this page', tocCount: '{n} sections',
      copy: 'Copy', copied: 'Copied', copyFailed: 'Copy failed', copyTitle: 'Copy code',
      prev: 'Previous', next: 'Next', home: 'Home',
      headingLevel: 'heading',
      switchTitle: 'Switch language',
      enFallback: 'English translation for this page is in progress. Showing the Chinese original.'
    }
  };

  function t(key) {
    var table = UI_TEXT[LANG] || UI_TEXT.zh;
    var value = table[key];
    return value === undefined ? UI_TEXT.zh[key] : value;
  }

  function fmt(text, values) {
    return text.replace(/\{(\w+)\}/g, function (_, key) {
      return values[key] !== undefined ? values[key] : '';
    });
  }

  function bandName(band) {
    return LANG === 'en' ? (band.name_en || band.name) : band.name;
  }

  function catName(cat) {
    return LANG === 'en' ? (cat.name_en || cat.name) : cat.name;
  }

  function enModule(mod) {
    return (window.DOCS_DATA_EN || {})[mod.id] || null;
  }

  function moduleTitle(mod) {
    if (LANG === 'en') {
      var en = enModule(mod);
      if (en && en.title) { return en.title; }
    }
    return mod.title;
  }

  function moduleContent(mod) {
    if (LANG === 'en') {
      var en = enModule(mod);
      if (en && en.content) { return en.content; }
    }
    return mod.content;
  }

  function hasEnglish(mod) {
    var en = enModule(mod);
    return !!(en && en.content);
  }

  // 按模块入口: 用户先选"要做什么", 再看最短使用路径
  var MODULE_ENTRIES = [
    {
      id: 'manager', cat: 'ecs', icon: 'E', name: 'ECS', name_en: 'ECS',
      desc: '实体、组件存储、视图与分组查询',
      desc_en: 'Entities, component storage, views and grouped queries',
      steps: ['创建实体', '添加组件', 'view 遍历'],
      steps_en: ['Create entity', 'Add components', 'Iterate with view'],
      entry: 'ecs/manager.hpp'
    },
    {
      id: 'reflection_usage', cat: 'reflection', icon: 'R', name: '反射', name_en: 'Reflection',
      desc: '类型元数据：字段读写、方法调用、枚举与继承',
      desc_en: 'Type metadata: field access, method calls, enums and inheritance',
      steps: ['注册类型与字段', 'get<T>() 读写', 'invoke 调用'],
      steps_en: ['Register type and fields', 'Read/write via get<T>()', 'Invoke methods'],
      entry: 'reflection/manager.hpp'
    },
    {
      id: 'serialization', cat: 'serialization', icon: 'Z', name: '序列化', name_en: 'Serialization',
      desc: '对象、数组与整存档的保存和还原',
      desc_en: 'Save and load objects, arrays and full world archives',
      steps: ['注册反射字段', 'save_object / save_world', 'load 还原'],
      steps_en: ['Register reflected fields', 'save_object / save_world', 'Load back'],
      entry: 'serialization/manager.hpp'
    },
    {
      id: 'thread_manager', cat: 'thread', icon: 'X', name: '多线程', name_en: 'Threading',
      desc: '工作线程池、持久任务图与帧循环',
      desc_en: 'Worker pool, persistent task graph and frame loop',
      steps: ['configure(profile)', 'submit / start', 'drain / stop'],
      steps_en: ['configure(profile)', 'submit / start', 'drain / stop'],
      entry: 'thread/manager.hpp'
    }
  ];

  var DOM = {};
  var allModules = [];
  var currentModule = null;
  var headingIndex = null;          // 全站小节索引, 首次搜索时构建
  var pendingSectionIndex = null;   // 跨页搜索跳转待定位的小节序号

  // === 初始化 ===
  function init() {
    cacheDom();
    applyLanguage();
    buildModuleList();
    renderSidebar();
    renderHero();
    bindEvents();
    initParticles();
    route();
    window.addEventListener('hashchange', route);
  }

  function applyLanguage() {
    document.documentElement.lang = LANG === 'en' ? 'en' : 'zh-CN';
    if (DOM.langToggle) {
      DOM.langToggle.textContent = LANG === 'en' ? '中文' : 'EN';
      DOM.langToggle.title = t('switchTitle');
    }
    if (DOM.searchInput) {
      DOM.searchInput.placeholder = t('searchPlaceholder');
    }
  }

  function toggleLanguage() {
    LANG = LANG === 'en' ? 'zh' : 'en';
    try { localStorage.setItem(LANG_KEY, LANG); } catch (e) { /* 忽略 */ }
    headingIndex = null;
    applyLanguage();
    renderSidebar();
    if (currentModule) {
      renderModule(currentModule);
    } else {
      renderHero();
    }
    updateSidebarActive();
  }

  function cacheDom() {
    DOM.sidebar      = document.querySelector('.sidebar');
    DOM.main         = document.querySelector('.main');
    DOM.content      = document.querySelector('.content');
    DOM.searchInput  = document.querySelector('.search-input');
    DOM.searchResults= document.querySelector('.search-results');
    DOM.backToTop    = document.querySelector('.back-to-top');
    DOM.themeToggle  = document.querySelector('.theme-toggle');
    DOM.langToggle   = document.querySelector('.lang-toggle');
    DOM.mobileMenuBtn= document.querySelector('.mobile-menu-btn');
  }

  // === 构建模块列表 (从 DOCS_DATA) ===
  function buildModuleList() {
    var data = window.DOCS_DATA || {};
    Object.keys(data).forEach(function (k) {
      allModules.push(data[k]);
    });
    allModules.sort(function (a, b) { return a.order - b.order; });
  }

  function modulesOf(catId) {
    return allModules.filter(function (m) { return m.category === catId; });
  }

  // === 渲染侧边栏: 库接口 / 宏配置 / 模块接口 ===
  function renderSidebar() {
    var html = '';
    BANDS.forEach(function (band) {
      var cats = CATEGORIES.filter(function (c) { return c.band === band.id; });
      var hasItem = cats.some(function (c) { return modulesOf(c.id).length > 0; });
      if (!hasItem) return;

      html += '<div class="sidebar-band">' + bandName(band) + '</div>';

      // 单分类分组直接平铺条目，避免“宏配置 / 宏配置 / 可选宏配置”三层重复
      if (cats.length === 1) {
        modulesOf(cats[0].id).forEach(function (m) {
          html += '<div class="sidebar-item" data-module="' + m.id + '">';
          html += '<span class="item-icon">' + m.icon + '</span>';
          html += '<span class="item-title">' + escapeText(moduleTitle(m)) + '</span>';
          html += '</div>';
        });
        return;
      }

      cats.forEach(function (cat) {
        var mods = modulesOf(cat.id);
        if (mods.length === 0) return;

        html += '<div class="sidebar-section cat-' + cat.id + '">';
        html += '<div class="sidebar-cat-label collapsed" data-cat="' + cat.id + '">';
        html += '<span class="dot"></span>';
        html += '<span>' + catName(cat) + '</span>';
        html += '<span class="chevron">▼</span>';
        html += '</div>';
        html += '<div class="sidebar-items collapsed" data-cat-items="' + cat.id + '">';
        mods.forEach(function (m) {
          html += '<div class="sidebar-item" data-module="' + m.id + '">';
          html += '<span class="item-icon">' + m.icon + '</span>';
          html += '<span class="item-title">' + escapeText(moduleTitle(m)) + '</span>';
          html += '</div>';
        });
        html += '</div>';
        html += '</div>';
      });
    });
    DOM.sidebar.innerHTML = html;
  }

  // === 渲染首页: 上库接口 / 中宏配置 / 下模块接口 ===
  function renderHero() {
    pendingSectionIndex = null;
    var total = allModules.length;
    var html = '';

    html += '<div class="hero page-transition">';
    html += '<h1>' + t('heroTitle') + '</h1>';
    html += '<p>' + fmt(t('heroSubtitle'), { n: total }) + '</p>';
    html += '</div>';

    // 按模块开始: 先选模块, 再看该模块的最短使用路径
    html += '<h2 class="home-section">' + t('entrySection') + '</h2>';
    html += '<div class="entry-grid">';
    MODULE_ENTRIES.forEach(function (entry) { html += entryCard(entry); });
    html += '</div>';

    // 阅读路径: 三步定位需要的接口
    html += '<h2 class="home-section">' + t('guideSection') + '</h2>';
    html += '<div class="hero-guide">';
    t('guide').forEach(function (step, index) {
      html += guideStep(String(index + 1), step[0], step[1], step[2]);
    });
    html += '</div>';

    html += '<div class="stats-grid">';
    html += statCard(total, t('statTotal'));
    html += statCard(CATEGORIES.length, t('statCategory'));
    html += statCard('C++20', t('statStandard'));
    html += statCard('0', t('statDeps'));
    html += '</div>';

    // 上部: 库接口
    html += '<h2>' + bandName(BANDS[0]) + '</h2>';
    html += '<div class="cat-grid api-grid">';
    CATEGORIES.filter(function (c) { return c.band === 'api'; }).forEach(function (cat) {
      html += catCard(cat, true);
    });
    html += '</div>';

    // 中部: 宏配置
    html += '<h2>' + bandName(BANDS[1]) + '</h2>';
    html += '<div class="cat-grid">';
    CATEGORIES.filter(function (c) { return c.band === 'config'; }).forEach(function (cat) {
      html += catCard(cat, false);
    });
    html += '</div>';

    // 下部: 模块接口
    html += '<h2>' + bandName(BANDS[2]) + '</h2>';
    html += '<div class="cat-grid">';
    CATEGORIES.filter(function (c) { return c.band === 'modules'; }).forEach(function (cat) {
      html += catCard(cat, false);
    });
    html += '</div>';

    DOM.content.innerHTML = html;

    DOM.content.querySelectorAll('[data-entry]').forEach(function (el) {
      el.addEventListener('click', function () {
        location.hash = '#/' + el.getAttribute('data-entry');
      });
    });

    document.querySelectorAll('[data-cat-link]').forEach(function (el) {
      el.addEventListener('click', function () {
        var cat = el.getAttribute('data-cat-link');
        var first = allModules.find(function (m) { return m.category === cat; });
        if (first) location.hash = '#/' + first.id;
      });
    });
  }

  function catCard(cat, withEntry) {
    var count = modulesOf(cat.id).length;
    var unit;
    if (LANG === 'en') {
      unit = cat.band === 'config' ? ' page' : ' modules';
    } else {
      unit = cat.band === 'config' ? ' 页' : ' 个模块';
    }
    var html = '';
    html += '<div class="cat-card cat-' + cat.id + '" data-cat-link="' + cat.id + '">';
    html += '<div class="cat-icon">' + cat.icon + '</div>';
    html += '<div class="cat-name">' + catName(cat) + '</div>';
    html += '<div class="cat-count">' + count + unit + '</div>';
    if (withEntry && cat.entry) {
      html += '<div class="cat-entry">' + cat.entry + '</div>';
    }
    html += '</div>';
    return html;
  }

  function statCard(num, label) {
    return '<div class="stat-card"><div class="num">' + num + '</div><div class="label">' + label + '</div></div>';
  }

  function guideStep(num, title, desc, note) {
    return '<div class="guide-step">'
      + '<span class="guide-num">' + num + '</span>'
      + '<div class="guide-body"><strong>' + title + '</strong>'
      + '<p>' + desc + '</p><span class="guide-note">' + note + '</span></div>'
      + '</div>';
  }

  function entryCard(entry) {
    var isEn = LANG === 'en';
    var name = isEn && entry.name_en ? entry.name_en : entry.name;
    var desc = isEn && entry.desc_en ? entry.desc_en : entry.desc;
    var list = isEn && entry.steps_en ? entry.steps_en : entry.steps;
    var steps = list.map(function (step) {
      return '<li>' + escapeText(step) + '</li>';
    }).join('');
    return '<div class="entry-card cat-' + entry.cat + '" data-entry="' + entry.id + '">'
      + '<div class="entry-head">'
      + '<span class="entry-icon">' + entry.icon + '</span>'
      + '<div class="entry-title"><strong>' + name + '</strong>'
      + '<span>' + entry.entry + '</span></div>'
      + '<span class="entry-go">' + (isEn ? 'Open →' : '进入 →') + '</span>'
      + '</div>'
      + '<p class="entry-desc">' + escapeText(desc) + '</p>'
      + '<ul class="entry-steps">' + steps + '</ul>'
      + '</div>';
  }

  // === 渲染模块内容 ===
  function renderModule(mod) {
    currentModule = mod;
    var cat = CATEGORIES.find(function (c) { return c.id === mod.category; });

    var html = '';
    html += '<div class="breadcrumb cat-' + mod.category + '">';
    html += '<span>' + (cat ? catName(cat) : mod.category) + '</span>';
    html += '<span class="sep">/</span>';
    html += '<span class="current">' + escapeText(moduleTitle(mod)) + '</span>';
    html += '<button class="btn home-btn" title="' + t('home') + '">';
    html += '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="m3 9 9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>';
    html += '<span>' + t('home') + '</span>';
    html += '</button>';
    html += '</div>';

    html += '<div class="doc-content page-transition cat-' + mod.category + '">';
    if (LANG === 'en' && !hasEnglish(mod)) {
      html += '<div class="doc-notice">' + t('enFallback') + '</div>';
    }
    html += window.MD.parse(moduleContent(mod));
    html += '</div>';

    DOM.content.innerHTML = html;
    DOM.main.scrollTop = 0;

    buildPageToc();
    enhanceCodeBlocks();
    buildPageFooter(mod);

    var homeBtn = DOM.content.querySelector('.home-btn');
    if (homeBtn) {
      homeBtn.addEventListener('click', function () {
        location.hash = '';
      });
    }

    DOM.content.querySelectorAll('.doc-content a[href^="#"]').forEach(function (link) {
      link.addEventListener('click', function (event) {
        var href = link.getAttribute('href');
        if (!href || href.indexOf('#/') === 0) return;
        var targetId = decodeURIComponent(href.slice(1));
        var target = document.getElementById(targetId);
        if (!target) {
          var compact = function (value) {
            return value.replace(/[^0-9a-zA-Z\u4e00-\u9fff]/g, '').toLowerCase();
          };
          var wanted = compact(targetId);
          target = Array.prototype.find.call(
            DOM.content.querySelectorAll('.doc-content [id]'),
            function (el) { return compact(el.id) === wanted; }
          );
        }
        if (!target) return;
        event.preventDefault();
        scrollToElement(target);
      });
    });

    updateSidebarActive();

    if (pendingSectionIndex !== null) {
      var sectionIndex = pendingSectionIndex;
      pendingSectionIndex = null;
      requestAnimationFrame(function () { revealSection(sectionIndex); });
    }
  }

  // === 页内导航: 从标题自动生成, 长页面默认折叠 ===
  function buildPageToc() {
    var doc = DOM.content.querySelector('.doc-content');
    if (!doc) return;

    var heads = doc.querySelectorAll('h1, h2, h3');
    if (heads.length < 2) return;

    var nav = document.createElement('nav');
    nav.className = 'page-toc';
    if (heads.length > 18) nav.classList.add('collapsed');

    var head = document.createElement('button');
    head.type = 'button';
    head.className = 'page-toc-head';
    head.innerHTML = '<span class="toc-label">' + t('tocTitle') + '</span>'
      + '<span class="toc-count">' + fmt(t('tocCount'), { n: heads.length }) + '</span>'
      + '<span class="toc-chevron">▼</span>';

    var body = document.createElement('div');
    body.className = 'page-toc-body';

    var links = '';
    Array.prototype.forEach.call(heads, function (node) {
      var level = parseInt(node.tagName.slice(1), 10);
      links += '<a class="toc-link toc-l' + level + '" href="#' + node.id + '">'
        + escapeText(node.textContent) + '</a>';
    });
    body.innerHTML = links;

    head.addEventListener('click', function () {
      nav.classList.toggle('collapsed');
    });

    nav.appendChild(head);
    nav.appendChild(body);
    doc.insertBefore(nav, doc.firstChild);
  }

  // === 代码块: 一键复制 ===
  function enhanceCodeBlocks() {
    Array.prototype.forEach.call(DOM.content.querySelectorAll('.doc-content pre'), function (pre) {
      if (pre.querySelector('.code-copy')) return;
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'code-copy';
      btn.textContent = t('copy');
      btn.title = t('copyTitle');
      btn.addEventListener('click', function () {
        var code = pre.querySelector('code');
        copyCode(code ? code.textContent : '', btn);
      });
      pre.classList.add('has-copy');
      pre.appendChild(btn);
    });
  }

  function copyCode(text, btn) {
    function done(ok) {
      btn.textContent = ok ? t('copied') : t('copyFailed');
      btn.classList.toggle('ok', ok);
      setTimeout(function () {
        btn.textContent = t('copy');
        btn.classList.remove('ok');
      }, 1200);
    }
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(function () { done(true); },
        function () { done(legacyCopy(text)); });
      return;
    }
    done(legacyCopy(text));
  }

  function legacyCopy(text) {
    var area = document.createElement('textarea');
    area.value = text;
    area.style.position = 'fixed';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.select();
    var ok = false;
    try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
    document.body.removeChild(area);
    return ok;
  }

  // === 页尾: 上一页 / 下一页 ===
  function buildPageFooter(mod) {
    var doc = DOM.content.querySelector('.doc-content');
    var index = allModules.indexOf(mod);
    if (!doc || index < 0) return;

    var prev = index > 0 ? allModules[index - 1] : null;
    var next = index + 1 < allModules.length ? allModules[index + 1] : null;

    var html = '<nav class="page-nav">';
    html += prev
      ? '<a class="page-nav-item prev" href="#/' + prev.id + '"><span>' + t('prev') + '</span><strong>'
        + escapeText(moduleTitle(prev)) + '</strong></a>'
      : '<span class="page-nav-item empty"></span>';
    html += next
      ? '<a class="page-nav-item next" href="#/' + next.id + '"><span>' + t('next') + '</span><strong>'
        + escapeText(moduleTitle(next)) + '</strong></a>'
      : '<span class="page-nav-item empty"></span>';
    html += '</nav>';

    doc.insertAdjacentHTML('beforeend', html);
  }

  // === 搜索跳转: 定位到指定小节 ===
  function scrollToElement(node) {
    if (!node) return;
    var main = DOM.main;
    var offset = node.getBoundingClientRect().top
      - main.getBoundingClientRect().top + main.scrollTop - 12;
    if (offset < 0) offset = 0;

    var reduceMotion = window.matchMedia
      && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (reduceMotion) {
      main.scrollTop = offset;
      return;
    }

    var before = main.scrollTop;
    main.scrollTo({ top: offset, behavior: 'smooth' });
    // 部分环境不支持容器内平滑滚动, 未产生位移时回退为立即定位
    setTimeout(function () {
      if (Math.abs(main.scrollTop - before) < 2 && Math.abs(before - offset) > 2) {
        main.scrollTop = offset;
      }
    }, 240);
  }

  function revealSection(ordinal) {
    var heads = DOM.content.querySelectorAll('.doc-content h1, .doc-content h2, .doc-content h3');
    var node = heads[ordinal];
    if (!node) return;
    scrollToElement(node);
    node.classList.remove('section-flash');
    void node.offsetWidth;
    node.classList.add('section-flash');
    setTimeout(function () { node.classList.remove('section-flash'); }, 1600);
  }

  function buildHeadingIndex() {
    if (headingIndex !== null) return headingIndex;
    headingIndex = [];
    allModules.forEach(function (mod) {
      var re = /^(#{1,3})\s+(.+)$/gm;
      var match;
      var ordinal = 0;
      var body = moduleContent(mod);
      while ((match = re.exec(body)) !== null) {
        var text = match[2].replace(/`/g, '').replace(/\*\*/g, '').trim();
        headingIndex.push({ mod: mod, text: text, level: match[1].length, ordinal: ordinal });
        ordinal++;
      }
    });
    return headingIndex;
  }

  function updateSidebarActive() {
    document.querySelectorAll('.sidebar-item').forEach(function (el) {
      el.classList.toggle('active', el.getAttribute('data-module') === (currentModule ? currentModule.id : ''));
    });
    if (currentModule) {
      var items = document.querySelector('[data-cat-items="' + currentModule.category + '"]');
      var label = document.querySelector('[data-cat="' + currentModule.category + '"]');
      if (items) items.classList.remove('collapsed');
      if (label) label.classList.remove('collapsed');
    }
  }

  // === 路由 ===
  function route() {
    var hash = location.hash.slice(2);
    if (!hash) {
      renderHero();
      currentModule = null;
      updateSidebarActive();
      return;
    }
    var mod = window.DOCS_DATA && window.DOCS_DATA[hash];
    if (mod) {
      renderModule(mod);
    } else {
      renderHero();
    }
  }

  // === 搜索 ===
  var searchTimer = null;
  function positionSearchResults() {
    var rect = DOM.searchInput.getBoundingClientRect();
    DOM.searchResults.style.top = (rect.bottom + 4) + 'px';
    DOM.searchResults.style.left = rect.left + 'px';
    DOM.searchResults.style.width = rect.width + 'px';
  }

  function onSearch() {
    var q = DOM.searchInput.value.trim().toLowerCase();
    if (!q) {
      DOM.searchResults.classList.remove('show');
      return;
    }

    positionSearchResults();
    clearTimeout(searchTimer);
    searchTimer = setTimeout(function () {
      var results = [];
      allModules.forEach(function (m) {
        var titleText = moduleTitle(m);
        var bodyText = moduleContent(m);
        var titleMatch = titleText.toLowerCase().indexOf(q);
        var contentMatch = bodyText.toLowerCase().indexOf(q);

        if (titleMatch >= 0) {
          results.push({ mod: m, rank: 0, snippet: '' });
        } else if (contentMatch >= 0) {
          var start = Math.max(0, contentMatch - 40);
          var snippet = (start > 0 ? '...' : '')
            + bodyText.substr(start, 100).replace(/\n/g, ' ') + '...';
          results.push({ mod: m, rank: 2, snippet: snippet });
        }
      });

      // 小节命中: 直接定位到对应标题
      buildHeadingIndex().forEach(function (h) {
        if (h.text.toLowerCase().indexOf(q) >= 0) {
          results.push({ mod: h.mod, rank: 1, heading: h, snippet: '' });
        }
      });

      results.sort(function (a, b) {
        if (a.rank !== b.rank) return a.rank - b.rank;
        return a.mod.order - b.mod.order;
      });

      renderSearchResults(results, q);
    }, 150);
  }

  function renderSearchResults(results, q) {
    positionSearchResults();
    if (results.length === 0) {
      DOM.searchResults.innerHTML = '<div class="search-empty">' + t('searchEmpty') + '</div>';
      DOM.searchResults.classList.add('show');
      return;
    }

    var html = '';
    results.slice(0, 20).forEach(function (r) {
      var title = r.heading ? highlight(r.heading.text, q) : highlight(moduleTitle(r.mod), q);
      var snippet = '';
      if (r.heading) {
        snippet = escapeText(moduleTitle(r.mod)) + ' · ' + r.heading.level + ' ' + t('headingLevel');
      } else if (r.snippet) {
        snippet = highlight(r.snippet, q);
      } else {
        snippet = escapeText(categoryName(r.mod));
      }
      html += '<div class="search-result-item" data-module="' + r.mod.id + '"';
      if (r.heading) html += ' data-heading="' + r.heading.ordinal + '"';
      html += '>';
      html += '<div class="search-result-title">' + title + '</div>';
      html += '<div class="search-result-snippet">' + snippet + '</div>';
      html += '</div>';
    });

    DOM.searchResults.innerHTML = html;
    DOM.searchResults.classList.add('show');

    DOM.searchResults.querySelectorAll('.search-result-item').forEach(function (el) {
      el.addEventListener('click', function () {
        var id = el.getAttribute('data-module');
        var heading = el.getAttribute('data-heading');
        DOM.searchInput.value = '';
        DOM.searchResults.classList.remove('show');

        if (heading !== null) {
          if (currentModule && currentModule.id === id) {
            revealSection(parseInt(heading, 10));
          } else {
            pendingSectionIndex = parseInt(heading, 10);
            location.hash = '#/' + id;
          }
          return;
        }
        location.hash = '#/' + id;
      });
    });
  }

  function categoryName(mod) {
    var cat = CATEGORIES.find(function (c) { return c.id === mod.category; });
    return cat ? catName(cat) : mod.category;
  }

  function highlight(text, q) {
    var escaped = escapeText(text);
    var re = new RegExp('(' + q.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'gi');
    return escaped.replace(re, '<mark>$1</mark>');
  }

  // === 事件绑定 ===
  function bindEvents() {
    DOM.sidebar.addEventListener('click', function (e) {
      var item = e.target.closest('.sidebar-item');
      if (item) {
        var id = item.getAttribute('data-module');
        location.hash = '#/' + id;
        DOM.sidebar.classList.remove('open');
        return;
      }
      var label = e.target.closest('.sidebar-cat-label');
      if (label) {
        var cat = label.getAttribute('data-cat');
        var items = document.querySelector('[data-cat-items="' + cat + '"]');
        if (items) items.classList.toggle('collapsed');
        label.classList.toggle('collapsed');
      }
    });

    DOM.searchInput.addEventListener('input', onSearch);
    DOM.searchInput.addEventListener('focus', onSearch);
    document.addEventListener('click', function (e) {
      if (!e.target.closest('.search-box')) {
        DOM.searchResults.classList.remove('show');
      }
    });

    window.addEventListener('scroll', function () {
      if (DOM.searchResults.classList.contains('show')) {
        DOM.searchResults.classList.remove('show');
      }
    }, true);
    window.addEventListener('resize', function () {
      if (DOM.searchResults.classList.contains('show')) {
        DOM.searchResults.classList.remove('show');
      }
    });

    DOM.main.addEventListener('scroll', function () {
      DOM.backToTop.classList.toggle('show', DOM.main.scrollTop > 300);
      if (DOM.searchResults.classList.contains('show')) {
        DOM.searchResults.classList.remove('show');
      }
    });
    DOM.backToTop.addEventListener('click', function () {
      DOM.main.scrollTo({ top: 0, behavior: 'smooth' });
    });

    if (DOM.themeToggle) {
      DOM.themeToggle.addEventListener('click', function () {
        var cur = document.body.getAttribute('data-theme');
        document.body.setAttribute('data-theme', cur === 'light' ? '' : 'light');
        DOM.themeToggle.textContent = cur === 'light' ? '🌙' : '☀️';
      });
    }

    if (DOM.langToggle) {
      DOM.langToggle.addEventListener('click', toggleLanguage);
    }

    if (DOM.mobileMenuBtn) {
      DOM.mobileMenuBtn.addEventListener('click', function () {
        DOM.sidebar.classList.toggle('open');
      });
    }

    document.addEventListener('keydown', function (e) {
      if (e.key === '/' && document.activeElement !== DOM.searchInput) {
        e.preventDefault();
        DOM.searchInput.focus();
      }
      if (e.key === 'Escape') {
        DOM.searchResults.classList.remove('show');
        DOM.searchInput.blur();
      }
    });
  }

  // === 粒子背景 ===
  function initParticles() {
    var bg = document.querySelector('.particle-bg');
    if (!bg) return;
    var count = 30;
    for (var i = 0; i < count; i++) {
      var p = document.createElement('div');
      p.className = 'particle';
      p.style.left = Math.random() * 100 + '%';
      p.style.animationDuration = (15 + Math.random() * 15) + 's';
      p.style.animationDelay = (Math.random() * 15) + 's';
      var colors = ['#00e5ff', '#4d7cff', '#a855f7', '#ec4899'];
      p.style.background = colors[i % colors.length];
      p.style.boxShadow = '0 0 4px ' + colors[i % colors.length];
      bg.appendChild(p);
    }
  }

  // === 工具 ===
  function escapeText(s) {
    var div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
