import QtQuick
import "Util.js" as U

// A playlist, or a generated mix (kind: "mix") shown the same way.
Item {
    id: root
    property string arg: ""
    property string kind: "playlist"
    readonly property bool isMix: kind === "mix"
    readonly property var info: {
        var rev = Playlists.revision + Library.revision
        if (isMix) return Library.mixInfo(arg)
        return Playlists.info(arg)
    }
    readonly property var items: {
        var rev = Playlists.revision + Library.revision
        if (isMix) return Library.tracksInfo(Library.mixPaths(arg))
        return Playlists.items(arg)
    }
    readonly property var paths: items.filter(function (t) { return !t.missing }).map(function (t) { return t.path })
    readonly property bool liked: !isMix && info.special === "liked"
    readonly property var covers: isMix ? (info.covers || []) : (info.covers || [])
    readonly property string subtitle: isMix ? (info.subtitle || "")
                                             : U.plural(info.count || 0, "song", "songs") + (info.durationMs > 0 ? ", " + U.fmtLong(info.durationMs) : "")

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.items

        header: Item {
            width: list.width
            height: 290
            Backdrop { anchors.fill: parent; key: root.covers.length ? root.covers[0] : ""; base: Pal.surfaceLow; strength: 0.7 }
            IconButton { x: 12; y: 6; icon: "arrow_back"; tint: Pal.onSurface; onClicked: Bus.back() }
            Item {
                id: art; x: 32; y: 58; width: 190; height: 190
                Mosaic { anchors.fill: parent; visible: !root.liked; covers: root.covers; radiusPct: 8 }
                Rectangle {
                    anchors.fill: parent; visible: root.liked; radius: 190 * 0.08 * Pal.rs
                    gradient: Gradient { GradientStop { position: 0; color: Pal.primaryContainer } GradientStop { position: 1; color: Pal.primary } }
                    Icon { anchors.centerIn: parent; name: "favorite"; size: 84; color: Pal.onPrimary }
                }
            }
            Column {
                anchors.left: art.right; anchors.leftMargin: 28
                anchors.right: parent.right; anchors.rightMargin: 28
                anchors.bottom: art.bottom
                spacing: 7
                Text { text: root.isMix ? "Mix" : "Playlist"; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant }
                Text { width: parent.width; text: root.info.name || root.info.title || ""; elide: Text.ElideRight
                       font.pixelSize: Math.round(40 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                Text { width: parent.width; text: root.subtitle; elide: Text.ElideRight; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant }
                Row {
                    spacing: 10
                    Pill { icon: "play"; text: "Play"; enabled: root.paths.length > 0; onClicked: Player.playFixed(root.paths, 0) }
                    Pill { icon: "shuffle"; text: "Shuffle"; filled: false; enabled: root.paths.length > 0; onClicked: { Player.markFixed(); Player.playPathsShuffled(root.paths) } }
                    Pill { visible: root.isMix; icon: "playlist_add"; text: "Save as playlist"; filled: false
                           onClicked: Bus.prompt("Save playlist", root.info.title || "", "newPlaylist", root.paths) }
                    IconButton { visible: !root.isMix && !root.liked; anchors.verticalCenter: parent.verticalCenter; icon: "edit"
                                 onClicked: Bus.prompt("Rename playlist", root.info.name || "", "renamePlaylist", root.arg) }
                    IconButton { visible: !root.isMix && !root.liked; anchors.verticalCenter: parent.verticalCenter; icon: "delete"
                                 onClicked: { Playlists.remove(root.arg); Bus.back() } }
                }
            }
        }

        footer: Item {
            width: list.width; height: root.items.length === 0 ? 160 : 24
            Text {
                anchors.centerIn: parent; visible: root.items.length === 0
                width: parent.width - 80; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: "Nothing here yet. Right-click any song, or use its \u22EE menu, and choose Add to playlist."
                font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
            }
        }

        delegate: TrackRow {
            width: list.width
            index: model.index
            path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
            cover: modelData.cover; quality: modelData.quality; lossless: modelData.lossless
            durationMs: modelData.durationMs; missing: !!modelData.missing
            showAlbum: true
            menuCtx: root.isMix ? ({}) : ({ playlistId: root.arg, index: model.index, count: root.items.length })
            onActivated: Player.playFixed(root.paths, root.paths.indexOf(modelData.path))
        }
    }
    ScrollIndicator { view: list; anchors.top: list.top; anchors.bottom: list.bottom; height: undefined }
}
