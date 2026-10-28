#pragma once
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>
#include <algorithm>

// Accent-folded, lower-cased text for matching ("Beyoncé" == "beyonce").
inline QString foldText(const QString& s)
{
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d) {
        const auto cat = c.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining || cat == QChar::Mark_Enclosing) continue;
        out.append(c.toLower());
    }
    return out;
}

// Levenshtein distance that gives up once it exceeds `maxD`.
inline int boundedEditDistance(const QString& a, const QString& b, int maxD)
{
    const int n = a.size(), m = b.size();
    if (std::abs(n - m) > maxD) return maxD + 1;
    QVector<int> prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j) prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        int rowMin = cur[0];
        for (int j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            rowMin = std::min(rowMin, cur[j]);
        }
        if (rowMin > maxD) return maxD + 1;
        std::swap(prev, cur);
    }
    return prev[m];
}

inline QStringList wordsOf(const QString& folded)
{
    QStringList out;
    QString cur;
    for (const QChar c : folded) {
        if (c.isLetterOrNumber()) cur.append(c);
        else if (!cur.isEmpty()) { out << cur; cur.clear(); }
    }
    if (!cur.isEmpty()) out << cur;
    return out;
}

// Does `token` occur in `field` starting at a word boundary?
inline bool hasWordPrefix(const QString& field, const QString& token)
{
    for (int i = field.indexOf(token); i >= 0; i = field.indexOf(token, i + 1))
        if (i == 0 || !field[i - 1].isLetterOrNumber()) return true;
    return false;
}

// "A feat. B; C / D" -> ["A", "B", "C", "D"]
inline QStringList splitArtists(QString s)
{
    static const QRegularExpression feat(QStringLiteral(R"(\s+(?:feat\.?|ft\.?|featuring|with)\s+)"), QRegularExpression::CaseInsensitiveOption);
    s.replace(feat, QStringLiteral(";"));
    s.replace(QLatin1Char('/'), QLatin1Char(';'));
    QStringList out;
    for (const QString& p : s.split(';', Qt::SkipEmptyParts)) {
        const QString t = p.trimmed();
        if (!t.isEmpty()) out << t;
    }
    return out;
}

// ---- matching titles across sources ------------------------------------------------------
// "Lucid Dreams (feat. X) - Remastered 2019" -> "lucid dreams"; used to decide that a catalog song
// and a file (or two catalog entries) are the same recording.
inline QString normTitleKey(const QString& title)
{
    static const QRegularExpression brackets(QStringLiteral(R"([\(\[][^\)\]]*[\)\]])"));
    static const QRegularExpression dash(QStringLiteral(R"(\s+-\s+.*$)"));
    static const QRegularExpression feat(QStringLiteral(R"(\s+(?:feat\.?|ft\.?|featuring|with)\s+.*$)"), QRegularExpression::CaseInsensitiveOption);
    QString s = title;
    s.remove(brackets);
    s.remove(dash);
    s.remove(feat);
    return wordsOf(foldText(s)).join(QLatin1Char(' '));
}

// Things that make two titles different recordings even when the plain title matches.
inline QString variantTag(const QString& title)
{
    static const QRegularExpression re(QStringLiteral(R"(\b(remix|live|acoustic|instrumental|demo|karaoke|sped up|slowed|reverb|cover|a cappella|acapella|orchestral|piano version)\b)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(title);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

// First credited artist, folded: "Juice WRLD & Marshmello" -> "juice wrld"
inline QString primaryArtistKey(const QString& artist)
{
    static const QRegularExpression amp(QStringLiteral(R"(\s+(?:&|and|x|,)\s+.*$)"), QRegularExpression::CaseInsensitiveOption);
    const QStringList parts = splitArtists(artist);
    QString first = parts.isEmpty() ? artist : parts.first();
    first.remove(amp);
    return foldText(first).simplified();
}

// ---- cleaning up titles from video / upload sites ------------------------------------------
// "Juice WRLD - Lucid Dreams (Official Video) [HD]" uploaded by "JuiceWRLDVEVO"
//   -> artist "Juice WRLD", title "Lucid Dreams"
// "Lucid Dreams" uploaded by "Juice WRLD - Topic" -> artist "Juice WRLD", title "Lucid Dreams"
// Used for songs that come from extensions (YouTube, SoundCloud...) so that matching against
// your library, the catalog and the metadata lookup works on real names.
inline QString stripUploaderNoise(QString s)
{
    static const QRegularExpression topic(QStringLiteral(R"(\s*[-–]\s*topic\s*$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression vevo(QStringLiteral(R"((?:\s*vevo|\s*official|\s*music)\s*$)"), QRegularExpression::CaseInsensitiveOption);
    s.remove(topic);
    s = s.trimmed();
    // only strip a trailing "VEVO" / "Official" when something is left over
    QString t = s;
    t.remove(vevo);
    if (!t.trimmed().isEmpty() && t.trimmed().size() >= 2) s = t.trimmed();
    return s.simplified();
}

inline QString stripTitleNoise(QString s)
{
    // bracketed junk: (Official Video), [HD], (Lyrics), (Audio), (Visualizer), (4K), (Explicit)...
    static const QRegularExpression junkBracket(
        QStringLiteral(R"(\s*[\(\[][^\)\]]*\b(?:official|video|audio|lyrics?|lyric video|visuali[sz]er|hd|hq|4k|explicit|clip|mv|m/v|music video|full song|full version|remaster(?:ed)?(?: \d{4})?)\b[^\)\]]*[\)\]])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pipeTail(QStringLiteral(R"(\s*[|｜]\s*.*$)"));
    static const QRegularExpression ytTail(QStringLiteral(R"(\s+-\s+(?:official (?:music )?video|official audio|lyric video|audio|video)\s*$)"), QRegularExpression::CaseInsensitiveOption);
    s.remove(junkBracket);
    s.remove(pipeTail);
    s.remove(ytTail);
    return s.simplified();
}

struct UploadMeta { QString artist, title; };

inline UploadMeta parseUploadMeta(const QString& rawTitle, const QString& rawUploader)
{
    const QString uploader = stripUploaderNoise(rawUploader);
    QString title = stripTitleNoise(rawTitle);
    // "Artist - Title" is the overwhelmingly common upload convention
    static const QRegularExpression dash(QStringLiteral(R"(^\s*(.+?)\s+[-–—]\s+(.+?)\s*$)"));
    const auto m = dash.match(title);
    if (m.hasMatch()) {
        const QString left = m.captured(1).trimmed(), right = m.captured(2).trimmed();
        // "Title - Remastered 2011" style tails belong to the title, not a new artist
        static const QRegularExpression tail(QStringLiteral(R"(^(?:remaster|remastered|live|acoustic|single version|radio edit|mono|stereo|version|edit)\b)"), QRegularExpression::CaseInsensitiveOption);
        if (!tail.match(right).hasMatch() && !left.isEmpty() && !right.isEmpty()) return {left, right};
    }
    return {uploader, title};
}
