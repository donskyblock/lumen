import QtQuick
import "Util.js" as U

Item {
    id: root
    property string arg: ""            // album key
    readonly property var info: Library.revision >= 0 ? Library.albumInfo(arg) : ({})
    readonly property var tracks: Library.revision >= 0 ? Library.albumTrackList(arg) : []
    readonly property var paths: tracks.map(function (t) { return t.path })

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.tracks

        header: Item {
            width: list.width
            height: 290

            Backdrop { anchors.fill: parent; key: root.info.cover || ""; base: Pal.surfaceLow; strength: 0.7 }
            IconButton { x: 12; y: 6; icon: "arrow_back"; tint: Pal.onSurface; onClicked: Bus.back() }
            Cover { id: art; x: 32; y: 58; width: 190; height: 190; key: root.info.cover || ""; radiusPct: 8 }
            Column {
                anchors.left: art.right; anchors.leftMargin: 28
                anchors.right: parent.right; anchors.rightMargin: 28
                anchors.bottom: art.bottom
                spacing: 7
                Text { text: "Album"; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant }
                Text { width: parent.width; text: root.info.title || ""; elide: Text.ElideRight
                       font.pixelSize: Math.round(40 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                Row {
                    spacing: 8
                    Item {
                        width: an.implicitWidth; height: an.implicitHeight
                        Text { id: an; text: root.info.artist || ""; font.pixelSize: Math.round(15 * Pal.fs); font.weight: Font.DemiBold
                               color: am.containsMouse ? Pal.primary : Pal.onSurface; font.underline: am.containsMouse }
                        MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: Bus.go("artist", (root.info.artist || "").split(/ feat\.? | ft\.? |;|\//i)[0].trim()) }
                    }
                    Text { visible: (root.info.year || 0) > 0; text: root.info.year; font.pixelSize: Math.round(15 * Pal.fs); color: Pal.onSurfaceVariant }
                }
                Row {
                    spacing: 6
                    Chip { text: U.plural(root.info.count || 0, "song", "songs") + ", " + U.fmtLong(root.info.durationMs || 0) }
                    Chip { text: root.info.quality || ""; accent: !!root.info.lossless; visible: text !== "" }
                }
                Row {
                    spacing: 10
                    Pill { icon: "play"; text: "Play"; onClicked: Player.playAlbum(root.arg) }
                    Pill { icon: "shuffle"; text: "Shuffle"; filled: false; onClicked: Player.playAlbum(root.arg, 0, true) }
                    IconButton { anchors.verticalCenter: parent.verticalCenter; icon: "playlist_add"; onClicked: Bus.pickPlaylist(root.paths) }
                    IconButton { anchors.verticalCenter: parent.verticalCenter; icon: "more_vert"
                                 onClicked: { var p = mapToItem(null, 0, 40); Bus.menu("album", { key: root.arg, artist: root.info.artist }, p.x, p.y) } }
                }
            }
        }

        delegate: TrackRow {
            width: list.width
            index: model.index
            number: modelData.trackNo
            path: modelData.path; title: modelData.title; artist: modelData.artist
            quality: modelData.quality; lossless: modelData.lossless; durationMs: modelData.durationMs
            showCover: false
            showArtist: modelData.artist !== root.info.artist
            height: showArtist ? Math.round(60 * Pal.dens) : Math.round(50 * Pal.dens)
            onActivated: Player.playAlbum(root.arg, model.index)
        }
    }
    ScrollIndicator { view: list; anchors.top: list.top; anchors.bottom: list.bottom; height: undefined }
}
