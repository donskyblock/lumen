import QtQuick

Item {
    id: root
    signal openAlbum(int row)

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        readonly property int cols: Math.max(2, Math.floor(width / 190))
        cellWidth: Math.floor(width / cols)
        cellHeight: cellWidth + 58
        clip: true
        cacheBuffer: 300
        boundsBehavior: Flickable.StopAtBounds
        model: Library.albums

        delegate: Item {
            id: cell
            width: grid.cellWidth
            height: grid.cellHeight
            activeFocusOnTab: true
            Keys.onReturnPressed: root.openAlbum(index)
            Keys.onSpacePressed: root.openAlbum(index)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: 24
                color: Pal.onSurface
                opacity: hover.containsMouse ? 0.07 : 0
                border.width: cell.activeFocus ? 2 : 0
                border.color: Pal.primary
                Behavior on opacity { NumberAnimation { duration: 100 } }
            }
            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Cover {
                    width: parent.width
                    height: width
                    key: model.cover
                    radiusPct: 14
                }
                Column {
                    width: parent.width
                    spacing: 1
                    Text {
                        width: parent.width
                        text: model.title
                        elide: Text.ElideRight
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: Pal.onSurface
                    }
                    Text {
                        width: parent.width
                        text: model.artist
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        color: Pal.onSurfaceVariant
                    }
                }
            }
            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.openAlbum(index)
            }
        }
    }
    ScrollIndicator { view: grid; anchors.top: grid.top; anchors.bottom: grid.bottom; height: undefined }
}
