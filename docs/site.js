// Copy buttons on the programs page, and the contents list following the page.
document.querySelectorAll('.copy').forEach(function (b) {
  b.addEventListener('click', function () {
    var pre = b.closest('.frame').querySelector('pre');
    var text = pre.textContent.replace(/\n?$/, '\n');
    var done = function () { b.textContent = 'copied'; b.classList.add('done');
      setTimeout(function () { b.textContent = 'copy'; b.classList.remove('done'); }, 1500); };
    if (navigator.clipboard) navigator.clipboard.writeText(text).then(done, function () {});
  });
});
var links = Array.prototype.slice.call(document.querySelectorAll('.side a[href^="#"]'));
if (links.length && 'IntersectionObserver' in window) {
  var byId = {};
  links.forEach(function (a) { byId[a.getAttribute('href').slice(1)] = a; });
  var seen = new IntersectionObserver(function (entries) {
    entries.forEach(function (e) {
      if (!e.isIntersecting) return;
      links.forEach(function (a) { a.classList.remove('now'); });
      var a = byId[e.target.id]; if (a) a.classList.add('now');
    });
  }, { rootMargin: '0px 0px -75% 0px' });
  Object.keys(byId).forEach(function (id) { var el = document.getElementById(id); if (el) seen.observe(el); });
}

// On a phone the contents box scrolls away with the page; once it has,
// the title bar offers a way back to the top.
var totop = document.querySelector('.totop'), box = document.querySelector('.side nav');
if (totop && box) {
  var check = function () {
    totop.hidden = window.innerWidth > 820 || box.getBoundingClientRect().bottom > 0;
  };
  window.addEventListener('scroll', check, { passive: true });
  window.addEventListener('resize', check);
  check();
  totop.addEventListener('click', function (ev) { ev.preventDefault(); window.scrollTo({ top: 0, behavior: 'smooth' }); });
}
