import QtQuick
import "Util.js" as U

// Results for the top-bar search: library (ranked, typo tolerant), playlists and
// any enabled "source" extensions.
Item {
    id: root
    property string query: ""
    property string tab: "all"
    property bool losslessOnly: false

    property var res: ({ songs: [], albums: [], artists: [], top: {}, fuzzy: false })
    property var playlists: []
    property var remote: []
    property var cat: ({ artists: [], albums: [], tracks: [] })
    property int catGen: 0
    property bool catLoading: false
    property int gen: 0
    readonly property string q: query.trim()
    readonly property var songs: res.songs || []
    readonly property var albums: res.albums || []
    readonly property var artists: res.artists || []
    readonly property bool anyRemote: remote.some(function (r) { return r.items.length > 0 })
    readonly property bool loadingRemote: remote.some(function (r) { return r.state === "loading" })
    readonly property var catArtists: cat.artists || []
    readonly property var catAlbums: cat.albums || []
    readonly property var catTracks: cat.tracks || []
    readonly property bool anyCat: catArtists.length + catAlbums.length + catTracks.length > 0
    readonly property bool nothing: q !== "" && songs.length + albums.length + artists.length + playlists.length === 0 && !anyRemote && !loadingRemote && !anyCat && !catLoading

    // Songs for the Top result: when both your library and the online catalog have matches they are
    // mixed fairly (your most played alternate with the most popular online ones, and a song you own
    // wins over an online copy of the same recording). Otherwise whichever side has songs.
    readonly property var blended: (songs.length > 0 && catTracks.length > 0) ? Library.blendSongs(q, songs, catTracks, 8) : []
    readonly property var topSongs: blended.length > 0 ? blended : (songs.length > 0 ? songs : catTracks)
    readonly property bool topSongsOnline: songs.length === 0
    function isOnlineSong(s) { return s.online === true || (s.online === undefined && songs.length === 0) }

    // The best match overall: an artist whose name is what you typed beats everything; then a library
    // artist or album; otherwise the first of the mixed songs (library or online), then an online artist.
    readonly property var topItem: {
        var ql = q.toLowerCase()
        var ca = catArtists.length > 0 ? catArtists[0] : null
        if (ca && ca.name.toLowerCase() === ql) return { kind: "oartist", id: ca.id, name: ca.name, cover: ca.picture }
        if (res.top && res.top.kind !== undefined && res.top.kind !== "song") return res.top
        // A song query should open the actual song, not a weak artist-name hit
        // (for example, "Lucid Dreams" should lead with Juice WRLD's track).
        if (topSongs.length > 0) {
            var s = topSongs[0]
            return { kind: isOnlineSong(s) ? "otrack" : "song", path: s.path, title: s.title, artist: s.artist, cover: s.cover }
        }
        if (ca) return { kind: "oartist", id: ca.id, name: ca.name, cover: ca.picture }
        return ({})
    }
    function playTop(item) {
        if (isOnlineSong(item)) Player.playOnlineSong(item.path, paths(catTracks))
        else {
            var ps = paths(songs), at = ps.indexOf(item.path)
            Player.playPaths(ps, at < 0 ? 0 : at)
        }
    }
    function paths(list) { return list.map(function (s) { return s.path }) }

    function run() {
        if (q === "") { res = { songs: [], albums: [], artists: [], top: {}, fuzzy: false }; playlists = []; remote = []; cat = { artists: [], albums: [], tracks: [] }; catLoading = false; return }
        res = Library.searchAll(q, losslessOnly)
        playlists = Playlists.search(q)
        Plugins.search(q)
        catLoading = true
        catGen = Catalog.search(q)
    }
    onQChanged: { debounce.restart(); SongIndex.noteQuery(q) }
    onLosslessOnlyChanged: run()
    Component.onCompleted: run()
    Timer { id: debounce; interval: 120; onTriggered: root.run() }

    Connections {
        target: Catalog
        function onSearchDone(g, result) {
            if (g !== root.catGen) return
            root.cat = { artists: result.artists || [], albums: result.albums || [], tracks: result.tracks || [] }
            root.catLoading = false
        }
    }
    Connections {
        target: Plugins
        function onSearchStarted(g, ids) {
            root.gen = g
            root.remote = ids.map(function (id) { return { pluginId: id, name: "", state: "loading", items: [], error: "" } })
        }
        function onSearchResult(g, id, name, items, error) {
            if (g !== root.gen) return
            root.remote = root.remote.map(function (r) {
                return r.pluginId === id ? { pluginId: id, name: name, state: error ? "error" : "done", items: items, error: error } : r
            })
        }
    }

    // filter row
    Row {
        id: tabs
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 24
        height: 52
        spacing: 8
        Repeater {
            model: [{ k: "all", t: "All" }, { k: "songs", t: "Songs" }, { k: "albums", t: "Albums" }, { k: "artists", t: "Artists" },
                    { k: "playlists", t: "Playlists" }, { k: "online", t: "Online" }]
            delegate: FilterChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: modelData.k !== "online" || root.remote.length > 0
                text: modelData.t
                checked: root.tab === modelData.k
                onClicked: root.tab = modelData.k
            }
        }
        Rectangle { width: 1; height: 22; anchors.verticalCenter: parent.verticalCenter; color: Pal.outlineVariant }
        FilterChip { anchors.verticalCenter: parent.verticalCenter; text: "Lossless only"; checked: root.losslessOnly; onClicked: root.losslessOnly = !root.losslessOnly }
    }

    Flickable {
        id: scroller
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: width
        contentHeight: col.height + 30
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: scroller.width
            spacing: 2

            Text {
                x: 28; width: parent.width - 56; wrapMode: Text.WordWrap
                visible: root.res.fuzzy === true && !root.nothing && (root.songs.length + root.albums.length + root.artists.length) > 0
                text: "No exact matches, so these are the closest to \u201c" + root.q + "\u201d."
                font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant; height: visible ? implicitHeight + 10 : 0
            }
            Item {
                width: parent.width; height: 200; visible: root.q === ""
                Column {
                    anchors.centerIn: parent; spacing: 10; width: Math.min(420, parent.width - 48)
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "search"; size: 40; color: Pal.outline }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "Search your music"
                           font.pixelSize: Math.round(20 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
                }
            }
            Item {
                width: parent.width; height: 180; visible: root.nothing
                Column {
                    anchors.centerIn: parent; spacing: 8
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "No results for \u201c" + root.q + "\u201d"; font.pixelSize: Math.round(18 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.losslessOnly ? "Try turning off \u201cLossless only\u201d." : "Check the spelling, or try fewer words."
                           font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant }
                }
            }

            // ---- top result + best songs ----
            Row {
                x: 28; width: parent.width - 56; spacing: 20
                visible: root.tab === "all" && root.topItem.kind !== undefined
                Column {
                    width: Math.min(340, parent.width * 0.4)
                    Text { height: 40; verticalAlignment: Text.AlignVCenter; text: "Top result"; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                    Rectangle {
                        id: topCard
                        readonly property var t: root.topItem
                        width: parent.width; height: 210; radius: 24 * Pal.rs
                        color: tcm.containsMouse ? Pal.surfaceHighest : Pal.surfaceHigh
                        Cover { id: tcArt; x: 20; y: 20; width: 96; height: 96; key: topCard.t.cover || ""; circle: topCard.t.kind === "artist" || topCard.t.kind === "oartist"; radiusPct: 12 }
                        Column {
                            x: 20; y: 128; width: parent.width - 40; spacing: 4
                            Text { width: parent.width; elide: Text.ElideRight; text: (topCard.t.kind === "artist" || topCard.t.kind === "oartist") ? topCard.t.name : (topCard.t.title || "")
                                   font.pixelSize: Math.round(26 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                            Row {
                                spacing: 6
                                Chip { text: (topCard.t.kind === "artist" || topCard.t.kind === "oartist") ? "Artist" : (topCard.t.kind === "album" ? "Album" : "Song") }
                                Text { anchors.verticalCenter: parent.verticalCenter; visible: topCard.t.kind !== "artist" && topCard.t.kind !== "oartist"; text: topCard.t.artist || ""
                                       font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant }
                            }
                        }
                        Rectangle {
                            width: 48; height: 48; radius: 24 * Pal.rs; color: Pal.primary
                            anchors.right: parent.right; anchors.rightMargin: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 16
                            opacity: tcm.containsMouse ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Pal.motion ? 120 : 0 } }
                            Icon { anchors.centerIn: parent; name: "play"; size: 26; color: Pal.onPrimary }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var t = topCard.t
                                    if (t.kind === "oartist") Bus.playOnline("artist", t.id)
                                    else if (t.kind === "artist") Player.playPaths(Library.artistPaths(t.name), 0)
                                    else if (t.kind === "album") Player.playAlbum(t.key)
                                    else if (t.kind === "otrack") Player.playOnlineSong(t.path, root.paths(root.catTracks))
                                    else Player.playPaths([t.path], 0)
                                } }
                        }
                        MouseArea { id: tcm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; z: -1
                            onClicked: {
                                var t = topCard.t
                                if (t.kind === "oartist") Bus.go("oartist", t.id)
                                else if (t.kind === "artist") Bus.go("artist", t.name)
                                else if (t.kind === "album") Bus.go("album", t.key)
                                else if (t.kind === "otrack") Player.playOnlineSong(t.path, root.paths(root.catTracks))
                                else Player.playPaths([t.path], 0)
                            } }
                    }
                }
                Column {
                    width: parent.width - Math.min(340, parent.width * 0.4) - 20
                    visible: root.topSongs.length > 0
                    Text { height: 40; verticalAlignment: Text.AlignVCenter; text: "Songs"; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                    Repeater {
                        model: root.topSongs.slice(0, 4)
                        delegate: TrackRow {
                            width: parent.width + 56
                            x: -28
                            index: model.index; path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
                            cover: modelData.cover; quality: modelData.quality; lossless: modelData.lossless; durationMs: modelData.durationMs
                            onActivated: root.playTop(modelData)
                        }
                    }
                }
            }

            // ---- songs tab ----
            Repeater {
                model: root.tab === "songs" ? root.songs : []
                delegate: TrackRow {
                    width: col.width
                    index: model.index; path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
                    cover: modelData.cover; quality: modelData.quality; lossless: modelData.lossless; durationMs: modelData.durationMs
                    showAlbum: true
                    onActivated: Player.playPaths(root.paths(root.songs), model.index)
                }
            }

            // ---- shelves (all) / grids (single tab) ----
            Shelf {
                width: col.width; circle: true
                visible: root.tab === "all" && count > 0
                title: "Artists"
                model: root.tab === "all" ? root.artists : []
                card: Component { MediaCard { circle: true; title: modelData.name; cover: modelData.cover
                                              subtitle: modelData.albumCount + (modelData.albumCount === 1 ? " album" : " albums")
                                              onClicked: Bus.go("artist", modelData.name)
                                              onPlayClicked: Player.playPaths(Library.artistPaths(modelData.name), 0)
                                              onMenuRequested: (x, y) => Bus.menu("artist", { name: modelData.name }, x, y) } }
            }
            Shelf {
                width: col.width
                visible: root.tab === "all" && count > 0
                title: "Albums"
                model: root.tab === "all" ? root.albums : []
                card: Component { MediaCard { title: modelData.title; subtitle: modelData.artist; cover: modelData.cover
                                              onClicked: Bus.go("album", modelData.key)
                                              onPlayClicked: Player.playAlbum(modelData.key)
                                              onMenuRequested: (x, y) => Bus.menu("album", { key: modelData.key, artist: modelData.artist }, x, y) } }
            }
            Shelf {
                width: col.width
                visible: root.tab === "all" && count > 0
                title: "Playlists"
                model: root.tab === "all" ? root.playlists : []
                card: Component { MediaCard { title: modelData.name; covers: modelData.covers
                                              subtitle: modelData.count + (modelData.count === 1 ? " song" : " songs")
                                              onClicked: Bus.go("playlist", modelData.id)
                                              onPlayClicked: Player.playPaths(Playlists.playablePaths(modelData.id), 0)
                                              onMenuRequested: (x, y) => Bus.menu("playlist", { id: modelData.id, special: modelData.special }, x, y) } }
            }
            Flow {
                x: 20; width: col.width - 40
                visible: root.tab === "albums" || root.tab === "artists" || root.tab === "playlists"
                Repeater {
                    model: root.tab === "albums" ? root.albums : (root.tab === "artists" ? root.artists : (root.tab === "playlists" ? root.playlists : []))
                    delegate: MediaCard {
                        circle: root.tab === "artists"
                        title: root.tab === "albums" ? modelData.title : modelData.name
                        subtitle: root.tab === "albums" ? modelData.artist
                                  : (root.tab === "artists" ? modelData.albumCount + " albums" : modelData.count + " songs")
                        cover: modelData.cover || ""
                        covers: modelData.covers || []
                        onClicked: root.tab === "albums" ? Bus.go("album", modelData.key) : (root.tab === "artists" ? Bus.go("artist", modelData.name) : Bus.go("playlist", modelData.id))
                        onPlayClicked: root.tab === "albums" ? Player.playAlbum(modelData.key)
                                       : (root.tab === "artists" ? Player.playPaths(Library.artistPaths(modelData.name), 0) : Player.playPaths(Playlists.playablePaths(modelData.id), 0))
                    }
                }
            }

            // ---- online catalog ----
            Text {
                x: 28; height: 34; verticalAlignment: Text.AlignVCenter
                visible: root.catLoading && !root.anyCat && root.q !== ""
                text: "Searching the online catalog\u2026"; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
            }
            Shelf {
                width: col.width; circle: true
                visible: (root.tab === "all" || root.tab === "artists") && count > 0
                title: "Artists online"
                subtitle: "Open one to see every song and album, and play right away"
                model: root.catArtists
                card: Component { MediaCard { circle: true; title: modelData.name; cover: modelData.picture
                                              subtitle: modelData.fans > 0 ? Number(modelData.fans).toLocaleString(Qt.locale(), "f", 0) + " fans" : ""
                                              onClicked: Bus.go("oartist", modelData.id)
                                              onPlayClicked: Bus.playOnline("artist", modelData.id)
                                              onMenuRequested: (x, y) => Bus.menu("oartist", { id: modelData.id, name: modelData.name }, x, y) } }
            }
            Column {
                width: col.width
                visible: (root.tab === "songs" || (root.tab === "all" && !root.topSongsOnline)) && root.catTracks.length > 0
                Text { x: 28; height: 52; verticalAlignment: Text.AlignBottom; text: "Songs online"; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                Repeater {
                    model: (root.tab === "all" ? root.catTracks.slice(0, 8) : root.catTracks)
                    delegate: TrackRow {
                        width: col.width
                        index: model.index; path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
                        cover: modelData.cover; quality: ""; lossless: false; durationMs: modelData.durationMs
                        showAlbum: true
                        onActivated: Player.playOnlineSong(modelData.path, root.paths(root.catTracks))
                    }
                }
            }
            Shelf {
                width: col.width
                visible: (root.tab === "all" || root.tab === "albums") && count > 0
                title: "Albums online"
                model: root.catAlbums
                card: Component { MediaCard { title: modelData.title; subtitle: modelData.artist; cover: modelData.cover
                                              onClicked: Bus.go("oalbum", modelData.id)
                                              onPlayClicked: Bus.playOnline("album", modelData.id)
                                              onMenuRequested: (x, y) => Bus.menu("oalbum", { id: modelData.id, artistId: modelData.artistId }, x, y) } }
            }

            // ---- extensions ----
            Repeater {
                model: root.tab === "all" || root.tab === "online" ? root.remote : []
                delegate: Column {
                    width: col.width
                    readonly property var r: modelData
                    visible: r.state !== "done" || r.items.length > 0
                    Text { x: 28; height: 52; verticalAlignment: Text.AlignBottom; text: "From " + (r.name !== "" ? r.name : "extension")
                           font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                    Text { x: 28; height: 34; verticalAlignment: Text.AlignVCenter; visible: r.state === "loading"; text: "Searching\u2026"; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant }
                    Text { x: 28; width: col.width - 56; wrapMode: Text.WordWrap; visible: r.state === "error"
                           text: "This extension couldn't search: " + r.error; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.error }
                    Repeater {
                        model: r.items
                        delegate: Item {
                            id: rr
                            width: col.width; height: Math.round(60 * Pal.dens)
                            activeFocusOnTab: true
                            Keys.onReturnPressed: Player.playRemote(r.pluginId, modelData)
                            Rectangle { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; anchors.topMargin: 2; anchors.bottomMargin: 2
                                        radius: 18 * Pal.rs; color: rm.containsMouse ? Pal.surfaceHigh : "transparent"
                                        border.width: rr.activeFocus ? 2 : 0; border.color: Pal.primary }
                            Cover { id: rc; x: 20; anchors.verticalCenter: parent.verticalCenter; width: 44; height: 44; url: modelData.cover || ""; radiusPct: 20 }
                            Column {
                                anchors.left: rc.right; anchors.leftMargin: 14; anchors.right: rd.left; anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                Text { width: parent.width; text: modelData.title; elide: Text.ElideRight; font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                                Text { width: parent.width; text: (modelData.artist || "") + (modelData.album ? "  \u2022  " + modelData.album : ""); elide: Text.ElideRight; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
                            }
                            Text { id: rd; anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter
                                   width: 48; horizontalAlignment: Text.AlignRight
                                   text: modelData.durationMs > 0 ? U.fmt(modelData.durationMs) : ""; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
                            MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: Player.playRemote(r.pluginId, modelData) }
                        }
                    }
                }
            }
        }
    }
    ScrollIndicator { view: scroller; anchors.top: scroller.top; anchors.bottom: scroller.bottom; height: undefined }
}
