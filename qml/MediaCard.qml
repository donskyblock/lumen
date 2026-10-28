import QtQuick

// Album / playlist / mix / artist card with a hover play button.
Item {
    id: card
    property string title: ""
    property string subtitle: ""
    property string cover: ""
    property var covers: []             // 4 keys -> mosaic
    property bool circle: false
    property int cardWidth: Pal.cardW
    signal clicked()
    signal playClicked()
    signal menuRequested(real x, real y)

    width: cardWidth
    height: cardWidth + Math.round(58 * Pal.fs)
    activeFocusOnTab: true
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 22 * Pal.rs
        color: Pal.onSurface
        opacity: hover.containsMouse ? 0.07 : 0
        border.width: card.activeFocus ? 2 : 0
        border.color: Pal.primary
        Behavior on opacity { NumberAnimation { duration: Pal.motion ? 100 : 0 } }
    }
    Column {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        Item {
            id: art
            width: parent.width; height: width
            Mosaic { anchors.fill: parent; visible: card.covers.length >= 4; covers: card.covers; radiusPct: 14 }
            Cover { anchors.fill: parent; visible: card.covers.length < 4; key: card.cover !== "" ? card.cover : (card.covers.length > 0 ? card.covers[0] : ""); circle: card.circle; radiusPct: 14 }
            Rectangle {
                id: fab
                width: 48; height: 48; radius: width / 2
                anchors.right: parent.right; anchors.rightMargin: 8
                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                color: Pal.primary
                opacity: hover.containsMouse ? 1 : 0
                scale: hover.containsMouse ? 1 : 0.85
                Behavior on opacity { NumberAnimation { duration: Pal.motion ? 140 : 0 } }
                Behavior on scale { NumberAnimation { duration: Pal.motion ? 140 : 0; easing.type: Easing.OutBack } }
                Icon { anchors.centerIn: parent; name: "play"; size: 26; color: Pal.onPrimary }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.playClicked() }
            }
        }
        Column {
            width: parent.width
            spacing: 1
            Text {
                width: parent.width; text: card.title; elide: Text.ElideRight
                horizontalAlignment: card.circle ? Text.AlignHCenter : Text.AlignLeft
                font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface
            }
            Text {
                width: parent.width; text: card.subtitle; elide: Text.ElideRight
                horizontalAlignment: card.circle ? Text.AlignHCenter : Text.AlignLeft
                font.pixelSize: Math.round(13 * Pal.fs); color: Pal.onSurfaceVariant
            }
        }
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        z: -1
        onClicked: (m) => { if (m.button === Qt.RightButton) { var p = card.mapToItem(null, m.x, m.y); card.menuRequested(p.x, p.y) } else card.clicked() }
    }
}
