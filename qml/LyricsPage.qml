import QtQuick

Item {
    id: root

    Backdrop {
        anchors.fill: parent
        key: Player.cover
        base: Pal.surfaceLow
        strength: 0.5
        fadeTop: true
    }

    // header
    Item {
        id: head
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 72
        visible: Player.hasTrack
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 32
            anchors.right: chips.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            Text { width: parent.width; text: Player.title; elide: Text.ElideRight; font.pixelSize: 18; font.weight: Font.DemiBold; color: Pal.onSurface }
            Text { width: parent.width; text: Player.artist; elide: Text.ElideRight; font.pixelSize: 14; color: Pal.onSurfaceVariant }
        }
        Row {
            id: chips
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Chip { visible: Lyrics.state === "found" && Lyrics.synced; text: "Synced"; accent: true }
            Chip { visible: Lyrics.state === "found" && !Lyrics.synced; text: "Text only" }
            Chip { visible: Lyrics.state === "found" && Lyrics.source !== ""; text: Lyrics.source }
        }
    }

    ListView {
        id: list
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        visible: Lyrics.state === "found"
        clip: true
        model: Lyrics.lines
        currentIndex: Lyrics.synced ? Lyrics.current : -1
        highlightRangeMode: Lyrics.synced ? ListView.StrictlyEnforceRange : ListView.NoHighlightRange
        preferredHighlightBegin: height * 0.36
        preferredHighlightEnd: height * 0.36 + 56
        highlightMoveDuration: 380
        highlightMoveVelocity: -1
        boundsBehavior: Flickable.StopAtBounds
        header: Item { width: 1; height: Lyrics.synced ? list.height * 0.30 : 12 }
        footer: Item { width: 1; height: Lyrics.synced ? list.height * 0.5 : 40 }

        delegate: Item {
            id: ln
            width: list.width
            readonly property bool isCurrent: Lyrics.synced && index === Lyrics.current
            readonly property bool isEmpty: modelData.text === ""
            height: isEmpty ? 26 : txt.implicitHeight + 18
            Text {
                id: txt
                visible: !ln.isEmpty
                anchors.left: parent.left; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.text
                wrapMode: Text.WordWrap
                font.pixelSize: 30
                font.weight: Font.Bold
                lineHeight: 1.05
                color: ln.isCurrent ? Pal.onSurface : (Lyrics.synced ? Pal.onSurfaceVariant : Pal.onSurface)
                opacity: ln.isCurrent || !Lyrics.synced ? 1.0 : (index < Lyrics.current ? 0.28 : 0.5)
                Behavior on opacity { NumberAnimation { duration: 260 } }
                Behavior on color { ColorAnimation { duration: 260 } }
            }
            MouseArea {
                anchors.fill: parent
                enabled: Lyrics.synced && modelData.t >= 0
                cursorShape: Qt.PointingHandCursor
                onClicked: Player.seek(modelData.t)
            }
        }
    }

    // ---- states ----
    Column {
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 64)
        spacing: 12
        visible: Lyrics.state !== "found"

        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "lyrics"; size: 44; color: Pal.outline }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface
            text: !Player.hasTrack ? "Play something to see its lyrics"
                  : Lyrics.state === "loading" ? "Looking for lyrics\u2026"
                  : "No lyrics found"
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            font.pixelSize: 14; color: Pal.onSurfaceVariant
            visible: Player.hasTrack && Lyrics.state === "none"
            text: "Put a .lrc file next to the song, or turn on a lyrics extension in Settings. Lumen picks the closest match by title, artist and length."
        }
        Pill {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Player.hasTrack && Lyrics.state === "none"
            filled: false; icon: "refresh"; text: "Try again"
            onClicked: Lyrics.retry()
        }
    }
}
