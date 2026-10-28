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

// Cover keys for files are already base64url paths; online art arrives as a plain https address,
// which is encoded here the same way (no Qt.btoa: it is deprecated and noisy).
var _b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
var _coverCache = {}
function coverId(key) {
    if (key.indexOf("http") !== 0) return key
    var hit = _coverCache[key]
    if (hit !== undefined) return hit
    var bytes = [], i, c
    for (i = 0; i < key.length; ++i) {            // addresses are ASCII; anything else is percent-encoded already
        c = key.charCodeAt(i)
        bytes.push(c < 128 ? c : 63)
    }
    var out = ""
    for (i = 0; i < bytes.length; i += 3) {
        var n = (bytes[i] << 16) | ((bytes[i + 1] || 0) << 8) | (bytes[i + 2] || 0)
        out += _b64[(n >> 18) & 63] + _b64[(n >> 12) & 63]
        if (i + 1 < bytes.length) out += _b64[(n >> 6) & 63]
        if (i + 2 < bytes.length) out += _b64[n & 63]
    }
    _coverCache[key] = out
    return out
}
