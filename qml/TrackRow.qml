import QtQuick
import "Util.js" as U

// One song row, used by every list (songs, album, playlist, artist, search).
Item {
    id: row
    property int index: 0
    property string path: ""
    property string title: ""
    property string artist: ""
    property string album: ""
    property string cover: ""
    property string quality: ""
    property bool lossless: false
    property bool missing: false
    property real durationMs: 0
    property int number: 0               // shown instead of index when > 0
    property bool showCover: true
    property bool showAlbum: false
    property bool showArtist: true
    property var menuCtx: ({})            // extra context for the menu (playlist id, row...)
    signal activated()

    readonly property bool current: path !== "" && path === Player.currentPath
    readonly property bool wide: width > 640
    readonly property bool liked: Playlists.revision >= 0 && Playlists.isLiked(path)
    height: Math.round(60 * Pal.dens)
    activeFocusOnTab: true
    opacity: missing ? 0.45 : 1
    Keys.onReturnPressed: if (!missing) activated()
    Keys.onSpacePressed: if (!missing) activated()

    function openMenu(mx, my) {
        var p = row.mapToItem(null, mx, my)
        var c = row.menuCtx; c.path = row.path
        Bus.menu("track", c, p.x, p.y)
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 8; anchors.rightMargin: 8; anchors.topMargin: 2; anchors.bottomMargin: 2
        radius: 18 * Pal.rs
        color: row.current ? Pal.secondaryContainer : (hover.containsMouse ? Pal.surfaceHigh : "transparent")
        border.width: row.activeFocus ? 2 : 0
        border.color: Pal.primary
    }
    Item {
        id: num
        x: 16; width: 36; height: parent.height
        Icon {
            anchors.centerIn: parent
            visible: row.current || hover.containsMouse
            name: row.current && Player.playing ? "volume_up" : (row.current ? "pause" : "play")
            size: 18
            color: row.current ? Pal.primary : Pal.onSurfaceVariant
        }
        Text {
            anchors.centerIn: parent
            visible: !row.current && !hover.containsMouse
            text: row.number > 0 ? row.number : row.index + 1
            font.pixelSize: Math.round(13 * Pal.fs)
            color: Pal.onSurfaceVariant
        }
    }
    Cover {
        id: art
        visible: row.showCover
        x: 60; anchors.verticalCenter: parent.verticalCenter
        width: Math.round(44 * Pal.dens); height: width
        key: row.cover
        radiusPct: 20
    }
    // right-hand controls, laid out from the edge inwards
    Text {
        id: dur
        anchors.right: parent.right; anchors.rightMargin: 56
        anchors.verticalCenter: parent.verticalCenter
        width: 46; horizontalAlignment: Text.AlignRight
        text: U.fmt(row.durationMs); font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
    }
    IconButton {
        id: more
        anchors.right: parent.right; anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 36
        icon: "more_vert"; iconSize: 20
        opacity: hover.containsMouse || row.activeFocus || row.current ? 1 : 0
        onClicked: { var p = more.mapToItem(row, 0, 0); row.openMenu(p.x, p.y + 36) }
    }
    IconButton {
        id: heart
        anchors.right: dur.left; anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 36
        icon: row.liked ? "favorite" : "favorite_border"; iconSize: 20
        active: row.liked
        opacity: row.liked || hover.containsMouse ? 1 : 0
        onClicked: Playlists.toggleLike(row.path)
    }
    Chip {
        id: qual
        anchors.right: heart.left; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: row.wide && row.quality !== ""
        width: 164                       // fixed, so the album column lines up
        text: row.quality; accent: row.lossless
    }
    Text {
        id: alb
        anchors.right: qual.visible ? qual.left : heart.left
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        width: row.showAlbum && row.width > 880 ? Math.min(220, row.width * 0.2) : 0
        visible: width > 0
        text: row.album; elide: Text.ElideRight; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
    }
    Column {
        anchors.left: row.showCover ? art.right : num.right
        anchors.leftMargin: 14
        anchors.right: alb.visible ? alb.left : (qual.visible ? qual.left : heart.left)
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Text {
            width: parent.width
            text: row.title; elide: Text.ElideRight
            font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium
            font.strikeout: row.missing
            color: row.current ? Pal.onSecondaryContainer : Pal.onSurface
        }
        Text {
            width: parent.width
            visible: row.showArtist
            text: row.missing ? "File not found" : row.artist
            elide: Text.ElideRight; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
        }
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        z: -1
        onClicked: (m) => {
            if (m.button === Qt.RightButton) row.openMenu(m.x, m.y)
            else if (!row.missing) row.activated()
        }
    }
}
