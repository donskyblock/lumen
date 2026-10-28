import QtQuick
import "Util.js" as U

Rectangle {
    id: root
    property bool panelOpen: false
    property bool lyricsOpen: false
    signal togglePanel()
    signal toggleLyrics()

    radius: 28 * Pal.rs
    color: Pal.surfaceLow
    Behavior on color { ColorAnimation { duration: Pal.motion ? 500 : 0 } }

    // ---- now playing (left) ----
    Item {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(180, root.width * 0.26)
        height: 64
        visible: Player.hasTrack

        Cover {
            id: art
            width: 64; height: 64
            key: Player.cover
            url: Player.coverUrl
            radiusPct: 20
        }
        IconButton {
            id: heartBtn
            readonly property bool liked: Playlists.revision >= 0 && Playlists.isLiked(Player.currentPath)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 38; height: 38
            visible: Player.currentPath !== "" && Player.currentPath.indexOf("://") < 0
            icon: liked ? "favorite" : "favorite_border"; iconSize: 21
            active: liked
            onClicked: Playlists.toggleLike(Player.currentPath)
        }
        Column {
            anchors.left: art.right
            anchors.leftMargin: 14
            anchors.right: heartBtn.visible ? heartBtn.left : parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                width: parent.width
                text: Player.title
                elide: Text.ElideRight
                font.pixelSize: Math.round(15 * Pal.fs)
                font.weight: Font.DemiBold
                color: Pal.onSurface
            }
            Text {
                width: parent.width
                text: Player.artist
                elide: Text.ElideRight
                font.pixelSize: Math.round(13 * Pal.fs)
                color: Pal.onSurfaceVariant
            }
        }
    }

    // ---- transport (centre) ----
    Column {
        id: center
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(260, Math.min(640, root.width - left.width - right.width - 72))
        spacing: 0
        enabled: Player.hasTrack
        opacity: Player.hasTrack ? 1 : 0.4

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            height: 56
            spacing: 6
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "shuffle"; active: Player.shuffle
                onClicked: Player.toggleShuffle()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "skip_previous"; tint: Pal.onSurface; iconSize: 28
                onClicked: Player.previous()
            }
            // Expressive play button: squircle while playing, circle while paused.
            Item {
                width: 56; height: 56
                activeFocusOnTab: true
                Keys.onSpacePressed: Player.toggle()
                Keys.onReturnPressed: Player.toggle()
                Rectangle {
                    id: playShape
                    anchors.fill: parent
                    radius: (Player.playing ? 18 : 28) * Pal.rs
                    color: Pal.primary
                    Behavior on radius { NumberAnimation { duration: Pal.motion ? 260 : 0; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: playShape.radius
                    color: Pal.onPrimary
                    opacity: playMouse.pressed ? 0.16 : (playMouse.containsMouse ? 0.08 : 0)
                }
                Icon {
                    anchors.centerIn: parent
                    name: Player.playing ? "pause" : "play"
                    size: 30
                    color: Pal.onPrimary
                }
                MouseArea {
                    id: playMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Player.toggle()
                }
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "skip_next"; tint: Pal.onSurface; iconSize: 28
                onClicked: Player.next()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: Player.repeat === 2 ? "repeat_one" : "repeat"
                active: Player.repeat > 0
                onClicked: Player.cycleRepeat()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "autoplay"
                active: Player.autoplay
                opacity: Player.autoplayBusy ? 0.55 : 1
                Accessible.name: qsTr("Autoplay: keep playing similar songs")
                onClicked: Player.setAutoplay(!Player.autoplay)
            }
        }
        Item {
            width: parent.width
            height: 28
            Text {
                id: tNow
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 44
                horizontalAlignment: Text.AlignRight
                text: U.fmt(Player.position)
                font.pixelSize: Math.round(12 * Pal.fs)
                color: Pal.onSurfaceVariant
            }
            Scrubber {
                anchors.left: tNow.right
                anchors.leftMargin: 6
                anchors.right: tEnd.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                value: Player.duration > 0 ? Player.position / Player.duration : 0
                waving: Player.playing
                onSeekRequested: (f) => Player.seek(f * Player.duration)
            }
            Text {
                id: tEnd
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 44
                text: U.fmt(Player.duration)
                font.pixelSize: Math.round(12 * Pal.fs)
                color: Pal.onSurfaceVariant
            }
        }
    }

    // ---- queue + volume (right) ----
    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "lyrics"
            active: root.lyricsOpen
            onClicked: root.toggleLyrics()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "queue_music"
            active: root.panelOpen
            visible: root.width > 900
            onClicked: root.togglePanel()
        }
        IconButton {
            id: mute
            property real before: 0.8
            anchors.verticalCenter: parent.verticalCenter
            icon: Player.volume < 0.005 ? "volume_off" : "volume_up"
            onClicked: {
                if (Player.volume > 0.005) { before = Player.volume; Player.volume = 0 }
                else Player.volume = before
            }
        }
        Item {
            id: vol
            anchors.verticalCenter: parent.verticalCenter
            width: 104; height: 28
            activeFocusOnTab: true
            Keys.onLeftPressed: Player.volume = Player.volume - 0.05
            Keys.onRightPressed: Player.volume = Player.volume + 0.05
            Accessible.role: Accessible.Slider
            Accessible.name: "Volume"
            Rectangle {
                id: vTrack
                anchors.verticalCenter: parent.verticalCenter
                x: 6; width: parent.width - 12; height: 4; radius: 2
                color: Pal.outlineVariant
                Rectangle { width: parent.width * Player.volume; height: parent.height; radius: 2; color: Pal.primary }
            }
            Rectangle {
                width: 14; height: 14; radius: 7
                color: Pal.primary
                anchors.verticalCenter: parent.verticalCenter
                x: 6 + (vTrack.width) * Player.volume - 7
                border.width: vol.activeFocus ? 2 : 0
                border.color: Pal.onSurface
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                function setv(x) { Player.volume = Math.max(0, Math.min(1, (x - 6) / vTrack.width)) }
                onPressed: (m) => { vol.forceActiveFocus(); setv(m.x) }
                onPositionChanged: (m) => { if (pressed) setv(m.x) }
                onWheel: (w) => Player.volume = Player.volume + (w.angleDelta.y > 0 ? 0.04 : -0.04)
            }
        }
    }
}
