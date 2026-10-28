import QtQuick
import "Util.js" as U

Rectangle {
    id: root
    radius: 28 * Pal.rs
    color: Pal.surfaceLow
    Behavior on color { ColorAnimation { duration: Pal.motion ? 500 : 0 } }

    Backdrop {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 470
        key: Player.cover
        base: Pal.surfaceLow
        strength: 0.7
        cornerRadius: 28
    }
    Cover {
        id: art
        anchors.top: parent.top
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 40
        height: width
        key: Player.cover
        url: Player.coverUrl
        radiusPct: 9
    }
    Column {
        id: meta
        anchors.top: art.bottom
        anchors.topMargin: 18
        anchors.left: art.left
        anchors.right: art.right
        spacing: 3
        Text {
            width: parent.width
            text: Player.title
            elide: Text.ElideRight
            font.pixelSize: Math.round(22 * Pal.fs)
            font.weight: Font.Bold
            color: Pal.onSurface
        }
        Text {
            width: parent.width
            text: Player.artist
            elide: Text.ElideRight
            font.pixelSize: Math.round(15 * Pal.fs)
            color: Pal.onSurfaceVariant
        }
        Text {
            width: parent.width
            text: Player.album
            elide: Text.ElideRight
            font.pixelSize: Math.round(13 * Pal.fs)
            color: Pal.outline
        }
        Item { width: 1; height: 6 }
        Row {
            spacing: 4
            visible: Player.currentPath !== "" && Player.currentPath.indexOf("://") < 0
            IconButton {
                readonly property bool liked: Playlists.revision >= 0 && Playlists.isLiked(Player.currentPath)
                icon: liked ? "favorite" : "favorite_border"; active: liked
                onClicked: Playlists.toggleLike(Player.currentPath)
            }
            IconButton { icon: "playlist_add"; onClicked: Bus.pickPlaylist([Player.currentPath]) }
        }
        Flow {
            width: parent.width
            spacing: 6
            Chip { text: Player.lossless ? "Lossless" : "Lossy"; accent: Player.lossless }
            Chip { text: Player.quality }
        }
    }

    Text {
        id: upLabel
        anchors.top: meta.bottom
        anchors.topMargin: 22
        anchors.left: art.left
        text: "Up next"
        font.pixelSize: Math.round(15 * Pal.fs)
        font.weight: Font.DemiBold
        color: Pal.onSurface
        visible: Player.upNext.length > 0
    }
    ListView {
        id: queue
        anchors.top: upLabel.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: Player.upNext
        delegate: Item {
            width: queue.width
            height: 56
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8; anchors.rightMargin: 8
                radius: 16 * Pal.rs
                color: Pal.surfaceHigh
                opacity: qm.containsMouse ? 1 : 0
            }
            Cover {
                id: qc
                x: 16; anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40
                key: modelData.cover
                radiusPct: 20
            }
            Column {
                anchors.left: qc.right; anchors.leftMargin: 12
                anchors.right: parent.right; anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                Text { width: parent.width; text: modelData.title; elide: Text.ElideRight; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                Text { width: parent.width; text: modelData.artist; elide: Text.ElideRight; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant }
            }
            MouseArea { id: qm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Player.jumpTo(index) }
        }
    }
}
