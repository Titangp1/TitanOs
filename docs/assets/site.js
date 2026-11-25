/* TitanOs — site : animations au défilement, fonds d'écran calculés en direct, onglets. */
(() => {
  const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const clamp = (v, a = 0, b = 1) => Math.min(b, Math.max(a, v));
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];

  /* ---------------- fonds d'écran de TitanOs (même calcul que src/core/settings.c) ---------------- */
  const WALLS = [
    ["Titan", [0x0B1226, 0x1E1B4B, 0x4C1D95], [[.18, .22, .42, 0x3B82F6, .75], [.82, .30, .38, 0xA855F7, .80], [.60, .92, .45, 0xEC4899, .55], [.05, .85, .30, 0x22D3EE, .45]]],
    ["Océan", [0x03142B, 0x0C4A6E, 0x0E7490], [[.20, .25, .40, 0x0EA5E9, .70], [.85, .20, .35, 0x2DD4BF, .65], [.55, .90, .45, 0x3B82F6, .60], [.02, .80, .30, 0x22D3EE, .40]]],
    ["Aurore", [0x1E0B36, 0x831843, 0xC2410C], [[.15, .20, .40, 0xA855F7, .70], [.85, .25, .38, 0xF97316, .75], [.55, .95, .45, 0xEC4899, .65], [.40, .45, .25, 0xFBBF24, .35]]],
    ["Forêt", [0x041F14, 0x14532D, 0x4D7C0F], [[.20, .20, .40, 0x10B981, .65], [.85, .30, .38, 0x84CC16, .60], [.55, .95, .45, 0x14B8A6, .55], [.95, .90, .30, 0xFDE68A, .30]]],
    ["Nuit", [0x020617, 0x0F172A, 0x1E293B], [[.20, .20, .40, 0x1E3A8A, .70], [.85, .30, .35, 0x4338CA, .55], [.55, .95, .45, 0x0F766E, .45], [.95, .05, .25, 0x64748B, .30]]],
    ["Braise", [0x1C0A00, 0x7C2D12, 0xB45309], [[.18, .22, .40, 0xDC2626, .65], [.85, .25, .38, 0xF59E0B, .70], [.55, .95, .45, 0xF97316, .60], [.05, .85, .28, 0xFBBF24, .35]]],
  ];
  const rgb = c => [c >> 16 & 255, c >> 8 & 255, c & 255];

  /* dessine le fond n° idx dans une petite image (le navigateur l'agrandit en douceur) ;
   * t : décalage des taches de lumière, pour les faire dériver lentement */
  function paintWall(canvas, idx, t = 0) {
    const fw = canvas.width, fh = canvas.height;
    const ctx = canvas.getContext("2d");
    const img = ctx.createImageData(fw, fh);
    const [, cols, blobs] = WALLS[idx];
    const c = cols.map(rgb), diag = Math.hypot(fw, fh);
    const bl = blobs.map(([x, y, r, col, a], n) => ({
      x: x + Math.sin(t * 0.00021 + n * 1.7) * 0.05, y: y + Math.cos(t * 0.00017 + n * 2.3) * 0.05,
      r2: r * r * 0.5, col: rgb(col), a,
    }));
    let o = 0;
    for (let j = 0; j < fh; j++) {
      const fy = j / fh;
      for (let i = 0; i < fw; i++) {
        const fx = i / fw;
        const d = (fx * fw + fy * fh) / (fw + fh) * 2;
        const A = d < 1 ? c[0] : c[1], B = d < 1 ? c[1] : c[2], k = d < 1 ? d : d - 1;
        let r = A[0] + (B[0] - A[0]) * k, g = A[1] + (B[1] - A[1]) * k, b = A[2] + (B[2] - A[2]) * k;
        for (const p of bl) {
          const dx = (fx - p.x) * fw / diag, dy = (fy - p.y) * fh / diag;
          const q = (dx * dx + dy * dy) / p.r2, f = p.a / ((1 + q) * (1 + q));
          r += (p.col[0] - r) * f; g += (p.col[1] - g) * f; b += (p.col[2] - b) * f;
        }
        img.data[o++] = r; img.data[o++] = g; img.data[o++] = b; img.data[o++] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
  }
  function sizeWall(canvas, w = 192) {
    const r = canvas.getBoundingClientRect();
    canvas.width = w;
    canvas.height = Math.max(2, Math.round(w * (r.height || 9) / (r.width || 16)));
  }

  /* en-tête : le fond « Titan », dont les taches dérivent doucement */
  const heroWall = $(".hero__wall");
  if (heroWall) {
    sizeWall(heroWall, 160);
    paintWall(heroWall, 0, 0);
    if (!reduce) {
      let visible = true, last = 0;
      new IntersectionObserver(e => { visible = e[0].isIntersecting; }).observe(heroWall);
      const tick = now => {
        if (visible && now - last > 50) { paintWall(heroWall, 0, now); last = now; }
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    }
    addEventListener("resize", () => { sizeWall(heroWall, 160); paintWall(heroWall, 0, performance.now()); });
  }

  /* section « Fonds d'écran » : grand aperçu + vignettes */
  const walls = $("[data-walls]");
  if (walls) {
    const big = $(".walls__canvas", walls), name = $("[data-wall-name]", walls), picker = $("[data-wall-picker]", walls);
    let cur = 0;
    const show = i => {
      cur = i;
      sizeWall(big, 240);
      paintWall(big, i, 0);
      name.textContent = WALLS[i][0];
      $$("button", picker).forEach((b, k) => { b.classList.toggle("is-on", k === i); b.setAttribute("aria-selected", k === i); });
    };
    WALLS.forEach(([n], i) => {
      const b = document.createElement("button");
      b.type = "button";
      b.setAttribute("role", "tab");
      b.setAttribute("aria-label", n);
      const cv = document.createElement("canvas");
      cv.width = 74; cv.height = 48;
      b.append(cv);
      const label = document.createElement("span");
      label.textContent = n;
      b.append(label);
      b.addEventListener("click", () => show(i));
      picker.append(b);
      paintWall(cv, i, 0);
    });
    show(0);
    addEventListener("resize", () => show(cur));
    /* couleur d'accent : reprise par l'icône du dock et par la page */
    $$("[data-accents] button").forEach(b => b.addEventListener("click", () => {
      $$("[data-accents] button").forEach(x => x.classList.toggle("is-on", x === b));
      document.documentElement.style.setProperty("--accent", b.style.getPropertyValue("--c"));
    }));
  }

  /* ---------------- mots qui défilent ---------------- */
  const spin = $("[data-spinner]");
  if (spin && !reduce) {
    const words = $$("span", spin);
    let k = 0;
    setInterval(() => {
      k++;
      words.forEach(w => { w.style.transition = ""; w.style.transform = `translateY(${-100 * k}%)`; });
      if (k === words.length - 1) {          /* le dernier mot est le premier : retour invisible */
        setTimeout(() => {
          k = 0;
          words.forEach(w => { w.style.transition = "none"; w.style.transform = "translateY(0)"; });
        }, 950);
      }
    }, 2300);
  }

  /* ---------------- défilement : progression, navigation, effets ---------------- */
  const bar = $("[data-progress]"), nav = $("[data-nav]");
  const lines = $$("[data-line]");
  const shrink = $("[data-shrink]");
  const scrub = $("[data-scrub]"), laptop = $("[data-scrub-target]");
  const mask = $("[data-mask]"), maskImg = $("[data-mask-target]"), maskText = $("[data-mask-content]");
  const anchors = $$("[data-anchors] a[href^='#']").filter(a => a.getAttribute("href").length > 1 && a.getAttribute("href") !== "#haut");
  const targets = anchors.map(a => document.querySelector(a.getAttribute("href")));
  const hero = $(".hero");

  // progression dans une section « collante » : 0 en entrant, 1 en sortant
  const stickyProgress = el => {
    const r = el.getBoundingClientRect();
    return clamp(-r.top / (r.height - innerHeight));
  };

  let ticking = false;
  function onScroll() {
    ticking = false;
    const y = scrollY, vh = innerHeight;
    const max = document.documentElement.scrollHeight - vh;
    if (bar) bar.style.transform = `scaleX(${max > 0 ? y / max : 0})`;
    if (nav) {
      nav.classList.toggle("is-scrolled", y > 20);
      nav.classList.toggle("is-deep", hero ? y > hero.offsetHeight - 80 : y > vh);
    }
    if (reduce) return;
    // karaoké : chaque ligne s'allume en traversant le bas de l'écran
    for (const l of lines) {
      const r = l.getBoundingClientRect();
      l.style.setProperty("--p", clamp((vh * 0.82 - r.top) / (vh * 0.32)).toFixed(3));
    }
    // grand mot : il rétrécit en passant
    if (shrink) {
      const r = shrink.parentElement.getBoundingClientRect();
      const p = clamp((vh - r.top) / (vh + r.height));
      shrink.style.setProperty("--k", (1.75 - p * 1.0).toFixed(3));
    }
    // l'écran du portable grandit jusqu'à remplir la page
    if (scrub && laptop) {
      const p = stickyProgress(scrub), e = 1 - Math.pow(1 - p, 3);
      laptop.style.setProperty("--s", (0.62 + e * 0.5).toFixed(4));
    }
    // le logo s'ouvre sur l'image
    if (mask && maskImg) {
      const p = stickyProgress(mask);
      const grow = Math.pow(clamp(p / 0.72), 2.4);
      maskImg.style.setProperty("--m", `${22 + grow * 1600}vmin`);
      maskImg.style.setProperty("--shade", clamp((p - 0.6) / 0.25).toFixed(3));
      maskText.style.setProperty("--c", clamp((p - 0.66) / 0.2).toFixed(3));
    }
    // section en cours dans la barre d'ancres
    let cur = -1;
    targets.forEach((t, i) => { if (t && t.getBoundingClientRect().top < vh * 0.4) cur = i; });
    anchors.forEach((a, i) => a.classList.toggle("is-current", i === cur));
  }
  const request = () => { if (!ticking) { ticking = true; requestAnimationFrame(onScroll); } };
  addEventListener("scroll", request, { passive: true });
  addEventListener("resize", request);
  onScroll();

  /* ---------------- apparitions ---------------- */
  const io = new IntersectionObserver(entries => entries.forEach(e => {
    if (e.isIntersecting) { e.target.classList.add("is-in"); io.unobserve(e.target); }
  }), { threshold: 0.18, rootMargin: "0px 0px -6% 0px" });
  $$("[data-reveal]").forEach(el => io.observe(el));

  /* ---------------- onglets et étapes d'installation ---------------- */
  const tabs = $("[data-tabs]");
  if (tabs) {
    $$("[data-tab]", tabs).forEach(b => b.addEventListener("click", () => {
      $$("[data-tab]", tabs).forEach(x => x.setAttribute("aria-selected", x === b));
      $$("[data-panel]", tabs).forEach(p => { p.hidden = p.dataset.panel !== b.dataset.tab; });
    }));
  }
  const gallery = $("[data-gallery]");
  if (gallery) {
    const items = $$(".gallery__item", gallery), thumbs = $$(".gallery__menu button", gallery);
    let cur = 0, timer = null;
    const go = i => {
      cur = (i + items.length) % items.length;
      items.forEach((it, k) => it.classList.toggle("is-on", k === cur));
      thumbs.forEach((t, k) => t.classList.toggle("is-on", k === cur));
    };
    thumbs.forEach((t, i) => t.addEventListener("click", () => { go(i); clearInterval(timer); }));
    if (!reduce) {
      new IntersectionObserver(e => {
        clearInterval(timer);
        if (e[0].isIntersecting) timer = setInterval(() => go(cur + 1), 4200);
      }, { threshold: 0.4 }).observe(gallery);
    }
  }

  /* ---------------- FAQ : une seule réponse ouverte à la fois ---------------- */
  $$(".accordion details").forEach(d => d.addEventListener("toggle", () => {
    if (d.open) $$(".accordion details").forEach(o => { if (o !== d) o.open = false; });
  }));
})();
