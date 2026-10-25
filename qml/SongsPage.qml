import QtQuick
import "Util.js" as U

Item {
    id: root

    // Column geometry shared by the header and every row.
    readonly property int timeW: 48
    readonly property bool showQuality: width > 620
    readonly property bool showAlbum: width > 860
    readonly property int qualityW: 170
    readonly property int albumW: Math.min(240, width * 0.2)
    readonly property int gap: 16
    readonly property int timeX: width - 24 - timeW
    readonly property int qualityX: timeX - gap - qualityW
    readonly property int albumX: (showQuality ? qualityX : timeX) - gap - albumW
    readonly property int titleRight: showAlbum ? albumX - gap : (showQuality ? qualityX - gap : timeX - gap)

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36
        Text { x: 28; anchors.verticalCenter: parent.verticalCenter; text: "#"; font.pixelSize: 12; font.weight: Font.Medium; color: Pal.onSurfaceVariant }
        Text { x: 118; anchors.verticalCenter: parent.verticalCenter; text: "Title"; font.pixelSize: 12; font.weight: Font.Medium; color: Pal.onSurfaceVariant }
        Text { x: root.albumX; visible: root.showAlbum; anchors.verticalCenter: parent.verticalCenter; text: "Album"; font.pixelSize: 12; font.weight: Font.Medium; color: Pal.onSurfaceVariant }
        Text { x: root.qualityX; visible: root.showQuality; anchors.verticalCenter: parent.verticalCenter; text: "Quality"; font.pixelSize: 12; font.weight: Font.Medium; color: Pal.onSurfaceVariant }
        Text { x: root.timeX; width: root.timeW; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; text: "Time"; font.pixelSize: 12; font.weight: Font.Medium; color: Pal.onSurfaceVariant }
        Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 16; anchors.rightMargin: 16; height: 1; color: Pal.outlineVariant }
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        cacheBuffer: 400
        boundsBehavior: Flickable.StopAtBounds
        model: Library.songs

        delegate: Item {
            id: row
            width: list.width
            height: 60
            readonly property bool current: model.path === Player.currentPath
            activeFocusOnTab: true
            Keys.onReturnPressed: Player.playSong(index)
            Keys.onSpacePressed: Player.playSong(index)

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 2
                anchors.bottomMargin: 2
                radius: 18
                color: row.current ? Pal.secondaryContainer : (hover.containsMouse ? Pal.surfaceHigh : "transparent")
                border.width: row.activeFocus ? 2 : 0
                border.color: Pal.primary
            }
            Item {
                x: 16; width: 36; height: parent.height
                Icon {
                    anchors.centerIn: parent
                    visible: row.current
                    name: Player.playing ? "volume_up" : "pause"
                    size: 18
                    color: Pal.primary
                }
                Text {
                    anchors.centerIn: parent
                    visible: !row.current
                    text: index + 1
                    font.pixelSize: 13
                    color: Pal.onSurfaceVariant
                }
            }
            Cover {
                id: art
                x: 60; anchors.verticalCenter: parent.verticalCenter
                width: 44; height: 44
                key: model.cover
                radiusPct: 20
            }
            Column {
                x: art.x + art.width + 14
                width: root.titleRight - x
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: model.title
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: row.current ? Pal.onSecondaryContainer : Pal.onSurface
                }
                Text {
                    width: parent.width
                    text: model.artist
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    color: Pal.onSurfaceVariant
                }
            }
            Text {
                x: root.albumX; width: root.albumW
                visible: root.showAlbum
                anchors.verticalCenter: parent.verticalCenter
                text: model.album
                elide: Text.ElideRight
                font.pixelSize: 13
                color: Pal.onSurfaceVariant
            }
            Chip {
                x: root.qualityX
                visible: root.showQuality
                anchors.verticalCenter: parent.verticalCenter
                text: model.quality
                accent: model.lossless
            }
            Text {
                x: root.timeX; width: root.timeW
                horizontalAlignment: Text.AlignRight
                anchors.verticalCenter: parent.verticalCenter
                text: U.fmt(model.durationMs)
                font.pixelSize: 13
                color: Pal.onSurfaceVariant
            }
            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Player.playSong(index)
            }
        }
    }
    ScrollIndicator { view: list; anchors.top: list.top; anchors.bottom: list.bottom; height: undefined }
}
