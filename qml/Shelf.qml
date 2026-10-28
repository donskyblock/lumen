import QtQuick

// Titled horizontal row of cards (Spotify-style shelf).
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property var model
    property Component card
    property int cardWidth: Pal.cardW
    property bool circle: false
    visible: count > 0
    readonly property int count: model ? (model.length !== undefined ? model.length : 0) : 0
    height: visible ? head.height + list.height + 6 : 0

    Column {
        id: head
        x: 28; width: parent.width - 56
        spacing: 2
        Text { text: root.title; font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface; height: 40; verticalAlignment: Text.AlignBottom }
        Text { visible: root.subtitle !== ""; text: root.subtitle; font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant }
    }
    ListView {
        id: list
        anchors.top: head.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.cardWidth + Math.round(58 * Pal.fs) + 8
        orientation: ListView.Horizontal
        leftMargin: 20; rightMargin: 20
        spacing: 4
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.model
        delegate: root.card
        // wheel scrolls the page, not the shelf, unless shift is held
        WheelHandler {
            acceptedModifiers: Qt.ShiftModifier
            onWheel: (e) => list.flick(e.angleDelta.y * 8, 0)
        }
    }
}
