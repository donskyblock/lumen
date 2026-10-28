import QtQuick
import "Util.js" as U

// An album from the online catalog.
Item {
    id: root
    property string arg: ""            // catalog album id
    property var page: ({})
    property string error: ""
    readonly property var tracks: page.tracks || []
    readonly property bool allSaved: Catalog.savedRevision >= 0 && tracks.length > 0 && tracks.every(function (t) { return Catalog.isSaved(t.path) })
    function paths() { return tracks.map(function (t) { return t.path }) }

    function load() { page = ({}); error = ""; if (arg !== "") Catalog.loadAlbum(arg) }
    onArgChanged: load()
    Component.onCompleted: load()
    Connections {
        target: Catalog
        function onAlbumLoaded(id, p) { if (id === root.arg) root.page = p }
        function onLoadFailed(what, id, err) { if (what === "album" && id === root.arg) root.error = err }
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.tracks

        header: Item {
            width: list.width
            height: 290

            Backdrop { anchors.fill: parent; key: root.page.cover || ""; base: Pal.surfaceLow; strength: 0.7 }
            IconButton { x: 12; y: 6; icon: "arrow_back"; tint: Pal.onSurface; onClicked: Bus.back() }
            Cover { id: art; x: 32; y: 58; width: 190; height: 190; key: root.page.cover || ""; radiusPct: 8 }
            Column {
                anchors.left: art.right; anchors.leftMargin: 28
                anchors.right: parent.right; anchors.rightMargin: 28
                anchors.bottom: art.bottom
                spacing: 7
                Text { text: root.page.type === "single" ? "Single" : (root.page.type === "ep" ? "EP" : "Album"); font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant }
                Text { width: parent.width; text: root.page.title || (root.error === "" ? "Loading…" : ""); elide: Text.ElideRight
                       font.pixelSize: Math.round(40 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                Row {
                    spacing: 8
                    Item {
                        width: an.implicitWidth; height: an.implicitHeight
                        Text { id: an; text: root.page.artist || ""; font.pixelSize: Math.round(15 * Pal.fs); font.weight: Font.DemiBold
                               color: am.containsMouse ? Pal.primary : Pal.onSurface; font.underline: am.containsMouse }
                        MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: if (root.page.artistId) Bus.go("oartist", root.page.artistId) }
                    }
                    Text { visible: (root.page.year || 0) > 0; text: root.page.year || ""; font.pixelSize: Math.round(15 * Pal.fs); color: Pal.onSurfaceVariant }
                }
                Row {
                    spacing: 6
                    Chip { visible: root.tracks.length > 0; text: U.plural(root.tracks.length, "song", "songs") + ", " + U.fmtLong(root.page.durationMs || 0) }
                    Chip { visible: root.allSaved; accent: true; text: "In your library" }
                    Chip { visible: !Resolver.hasFullSource && root.tracks.length > 0; text: "30 s previews" }
                }
                Row {
                    spacing: 10
                    Pill { icon: "play"; text: "Play"; enabled: root.tracks.length > 0; onClicked: Player.playPaths(root.paths(), 0) }
                    Pill { icon: "shuffle"; text: "Shuffle"; filled: false; enabled: root.tracks.length > 0; onClicked: Player.playPathsShuffled(root.paths()) }
                    Pill {
                        icon: root.allSaved ? "check" : "add"; text: root.allSaved ? "In library" : "Add to library"; filled: false
                        enabled: root.tracks.length > 0
                        onClicked: root.allSaved ? Catalog.removeTracks(root.paths()) : Catalog.importAlbum(root.arg)
                    }
                    IconButton { anchors.verticalCenter: parent.verticalCenter; icon: "playlist_add"; enabled: root.tracks.length > 0; onClicked: Bus.pickPlaylist(root.paths()) }
                }
            }
        }

        delegate: TrackRow {
            width: list.width
            index: model.index
            number: modelData.trackNo
            path: modelData.path; title: modelData.title; artist: modelData.artist
            quality: ""; lossless: false; durationMs: modelData.durationMs
            showCover: false
            showArtist: modelData.artist !== root.page.artist
            height: showArtist ? Math.round(60 * Pal.dens) : Math.round(50 * Pal.dens)
            onActivated: Player.playPaths(root.paths(), model.index)
        }
        footer: Text {
            x: 28; width: list.width - 56; wrapMode: Text.WordWrap
            visible: root.error !== ""; height: visible ? implicitHeight + 30 : 0
            text: root.error; font.pixelSize: Math.round(15 * Pal.fs); color: Pal.error
        }
    }
    ScrollIndicator { view: list; anchors.top: list.top; anchors.bottom: list.bottom; height: undefined }
}
