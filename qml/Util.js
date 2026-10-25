.pragma library

function fmt(ms) {
    var s = Math.max(0, Math.floor(ms / 1000));
    var m = Math.floor(s / 60);
    var r = s % 60;
    return m + ":" + (r < 10 ? "0" : "") + r;
}

function fmtLong(ms) {
    var t = Math.round(ms / 60000);
    if (t < 60) return t + " min";
    return Math.floor(t / 60) + " h " + (t % 60) + " min";
}

function plural(n, one, many) {
    return n + " " + (n === 1 ? one : many);
}
