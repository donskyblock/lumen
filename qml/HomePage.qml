import QtQuick
import "Util.js" as U

Item {
    id: root
    property var hm: ({})
    readonly property var hidden: Profiles.active.homeHidden || []
    function shown(k) { return hidden.indexOf(k) < 0 }
    function refresh() { hm = Library.home() }
    Component.onCompleted: { refresh(); Catalog.loadCharts(); Taste.loadDiscover() }
    Timer { id: later; interval: 350; onTriggered: root.refresh() }
    Timer { id: discoverLater; interval: 2500; onTriggered: Taste.loadDiscover() }
    Connections { target: Taste; function onProfileChanged() { discoverLater.restart() } }
    readonly property var charts: Catalog.charts
    readonly property bool fresh: !hm.hasHistory && !Library.trackCount
    Connections { target: Library; function onChanged() { later.restart() } }
    Connections { target: Library.stats; function onChanged() { later.restart() } }
    Connections { target: Playlists; function onChanged() { later.restart() } }

    readonly property string greeting: {
        var h = new Date().getHours()
        return h < 5 ? "Still up?" : h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening"
    }

    // up to six things to jump straight back into
    readonly property var tiles: {
        var t = [], pl = Playlists.list, i
        for (i = 0; i < pl.length; ++i) if (pl[i].special === "liked") t.push({ kind: "playlist", id: pl[i].id, title: pl[i].name, covers: pl[i].covers })
        var rec = hm.recent || []
        for (i = 0; i < rec.length && t.length < 4; ++i) t.push({ kind: "album", key: rec[i].key, title: rec[i].title, cover: rec[i].cover })
        for (i = 0; i < pl.length && t.length < 6; ++i) if (pl[i].special !== "liked") t.push({ kind: "playlist", id: pl[i].id, title: pl[i].name, covers: pl[i].covers })
        var add = hm.added || []
        for (i = 0; i < add.length && t.length < 6; ++i) t.push({ kind: "album", key: add[i].key, title: add[i].title, cover: add[i].cover })
        return t
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.height + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: scroller.width
            spacing: 6

            Text { x: 28; height: 56; verticalAlignment: Text.AlignVCenter; text: root.greeting
                   font.pixelSize: Math.round(32 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }

            // quick tiles
            Grid {
                x: 28; width: parent.width - 56
                visible: root.shown("quick") && root.tiles.length > 0 && !root.fresh
                columns: width > 820 ? 3 : 2
                spacing: 10
                Repeater {
                    model: root.shown("quick") ? root.tiles : []
                    delegate: Item {
                        id: tile
                        width: (col.width - 56 - (parent.columns - 1) * 10) / parent.columns
                        height: 64
                        activeFocusOnTab: true
                        Keys.onReturnPressed: tm.clicked(null)
                        Rectangle { anchors.fill: parent; radius: 16 * Pal.rs; color: tm.containsMouse ? Pal.surfaceHighest : Pal.surfaceHigh
                                    border.width: tile.activeFocus ? 2 : 0; border.color: Pal.primary }
                        Mosaic { id: tc; width: 64; height: 64
                                 covers: modelData.kind === "album" ? [modelData.cover] : (modelData.covers || []); radiusPct: 18 }
                        Text { anchors.left: tc.right; anchors.leftMargin: 14; anchors.right: parent.right; anchors.rightMargin: 60
                               anchors.verticalCenter: parent.verticalCenter; text: modelData.title; elide: Text.ElideRight
                               font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
                        Rectangle {
                            width: 40; height: 40; radius: 20 * Pal.rs; color: Pal.primary
                            anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                            opacity: tm.containsMouse ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Pal.motion ? 120 : 0 } }
                            Icon { anchors.centerIn: parent; name: "play"; size: 22; color: Pal.onPrimary }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: modelData.kind === "album" ? Player.playAlbum(modelData.key) : Player.playFixed(Playlists.playablePaths(modelData.id), 0)
                            }
                        }
                        MouseArea {
                            id: tm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; z: -1
                            onClicked: modelData.kind === "album" ? Bus.go("album", modelData.key) : Bus.go("playlist", modelData.id)
                        }
                    }
                }
            }

            Text {
                x: 28; width: parent.width - 56; wrapMode: Text.WordWrap
                visible: root.fresh
                text: "Search for any artist above and press play. Everything you play teaches your radio what you like."
                font.pixelSize: Math.round(15 * Pal.fs); color: Pal.onSurfaceVariant
            }
            Row {
                x: 28; spacing: 10
                Pill { icon: "play"; text: "Play my radio"; enabled: !Taste.busy; onClicked: Taste.startRadio("taste") }
                Text { anchors.verticalCenter: parent.verticalCenter; visible: Taste.busy !== ""; text: Taste.busy
                       font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant }
            }

            Item { width: 1; height: 8 }

            Shelf {
                width: col.width; circle: true
                visible: count > 0
                title: "Discover"
                subtitle: Taste.discoverReason
                model: Taste.discover
                card: Component {
                    MediaCard {
                        circle: true
                        title: modelData.name; subtitle: modelData.because ? "Like " + modelData.because : ""; cover: modelData.picture
                        onClicked: Bus.go("oartist", modelData.id)
                        onPlayClicked: Bus.playOnline("artist", modelData.id)
                        onMenuRequested: (x, y) => Bus.menu("oartist", { id: modelData.id, name: modelData.name }, x, y)
                    }
                }
            }

            Shelf {
                width: col.width
                visible: root.shown("mixes") && count > 0
                title: "Made for you"
                subtitle: root.hm.hasHistory ? "Based on what you've been playing" : "Fresh picks from your library, until there's some listening history"
                model: root.shown("mixes") ? (root.hm.mixes || []) : []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.subtitle; covers: modelData.covers
                        onClicked: Bus.go("mix", modelData.id)
                        onPlayClicked: Player.playPaths(Library.mixPaths(modelData.id), 0)
                        onMenuRequested: (x, y) => Bus.menu("mix", { id: modelData.id }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                visible: root.shown("recent") && count > 0
                title: "Jump back in"
                model: root.shown("recent") ? (root.hm.recent || []) : []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.artist; cover: modelData.cover
                        onClicked: Bus.go("album", modelData.key)
                        onPlayClicked: Player.playAlbum(modelData.key)
                        onMenuRequested: (x, y) => Bus.menu("album", { key: modelData.key, artist: modelData.artist }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                visible: root.shown("added") && count > 0
                title: "Recently added"
                model: root.shown("added") ? (root.hm.added || []) : []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.artist; cover: modelData.cover
                        onClicked: Bus.go("album", modelData.key)
                        onPlayClicked: Player.playAlbum(modelData.key)
                        onMenuRequested: (x, y) => Bus.menu("album", { key: modelData.key, artist: modelData.artist }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                visible: root.shown("playlists") && count > 0 && !root.fresh
                title: "Your playlists"
                model: root.shown("playlists") ? Playlists.list : []
                card: Component {
                    MediaCard {
                        title: modelData.name; subtitle: modelData.count + (modelData.count === 1 ? " song" : " songs"); covers: modelData.covers
                        onClicked: Bus.go("playlist", modelData.id)
                        onPlayClicked: Player.playFixed(Playlists.playablePaths(modelData.id), 0)
                        onMenuRequested: (x, y) => Bus.menu("playlist", { id: modelData.id, special: modelData.special }, x, y)
                    }
                }
            }

            // most played songs
            Column {
                width: parent.width
                visible: root.shown("top") && (root.hm.topTracks || []).length > 0
                Text { x: 28; height: 52; verticalAlignment: Text.AlignBottom; text: "On repeat"; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                Repeater {
                    model: root.shown("top") ? (root.hm.topTracks || []).slice(0, 5) : []
                    delegate: TrackRow {
                        width: col.width
                        index: model.index; path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
                        cover: modelData.cover; quality: modelData.quality; lossless: modelData.lossless; durationMs: modelData.durationMs
                        showAlbum: true
                        onActivated: Player.playPaths((root.hm.topTracks || []).map(function (x) { return x.path }), model.index)
                    }
                }
            }

            Shelf {
                width: col.width
                visible: root.shown("artists") && count > 0
                circle: true
                title: root.hm.hasHistory ? "Artists you play" : "Artists"
                model: root.shown("artists") ? (root.hm.topArtists || []) : []
                card: Component {
                    MediaCard {
                        circle: true
                        title: modelData.name
                        subtitle: modelData.albumCount + (modelData.albumCount === 1 ? " album" : " albums")
                        cover: modelData.cover
                        onClicked: Bus.go("artist", modelData.name)
                        onPlayClicked: Player.playPaths(Library.artistPaths(modelData.name), 0)
                        onMenuRequested: (x, y) => Bus.menu("artist", { name: modelData.name }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width; circle: true
                visible: count > 0
                title: "Popular artists"
                subtitle: "Trending right now"
                model: root.charts.artists || []
                card: Component {
                    MediaCard {
                        circle: true
                        title: modelData.name; subtitle: ""; cover: modelData.picture
                        onClicked: Bus.go("oartist", modelData.id)
                        onPlayClicked: Bus.playOnline("artist", modelData.id)
                        onMenuRequested: (x, y) => Bus.menu("oartist", { id: modelData.id, name: modelData.name }, x, y)
                    }
                }
            }
            Column {
                width: parent.width
                visible: root.fresh && (root.charts.tracks || []).length > 0
                Text { x: 28; height: 52; verticalAlignment: Text.AlignBottom; text: "Top songs"; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                Repeater {
                    model: (root.charts.tracks || []).slice(0, 8)
                    delegate: TrackRow {
                        width: col.width
                        index: model.index; path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
                        cover: modelData.cover; quality: ""; lossless: false; durationMs: modelData.durationMs
                        showAlbum: true
                        onActivated: Player.playPaths((root.charts.tracks || []).map(function (x) { return x.path }), model.index)
                    }
                }
            }
            Shelf {
                width: col.width
                visible: count > 0
                title: "New and trending albums"
                model: root.charts.albums || []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.artist; cover: modelData.cover
                        onClicked: Bus.go("oalbum", modelData.id)
                        onPlayClicked: Bus.playOnline("album", modelData.id)
                        onMenuRequested: (x, y) => Bus.menu("oalbum", { id: modelData.id, artistId: modelData.artistId }, x, y)
                    }
                }
            }
        }
    }
    ScrollIndicator { view: scroller; anchors.top: scroller.top; anchors.bottom: scroller.bottom; height: undefined }
}
