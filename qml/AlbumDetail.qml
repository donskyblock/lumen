import QtQuick
import "Util.js" as U

Item {
    id: root
    property int row: -1
    signal back()

    readonly property var info: row >= 0 ? Library.albumInfo(row) : ({})
    readonly property var tracks: row >= 0 ? Library.albumTrackList(row) : []

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.tracks

        header: Item {
            width: list.width
            height: 268

            Backdrop {
                anchors.fill: parent
                key: root.info.cover || ""
                base: Pal.surfaceLow
                strength: 0.65
            }
            IconButton {
                id: backBtn
                x: 12; y: 0
                icon: "arrow_back"
                tint: Pal.onSurface
                onClicked: root.back()
            }
            Cover {
                id: art
                x: 24; y: 52
                width: 196; height: 196
                key: root.info.cover || ""
                radiusPct: 10
            }
            Column {
                anchors.left: art.right
                anchors.leftMargin: 24
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.bottom: art.bottom
                spacing: 6
                Text {
                    width: parent.width
                    text: root.info.title || ""
                    elide: Text.ElideRight
                    font.pixelSize: 34
                    font.weight: Font.Bold
                    color: Pal.onSurface
                }
                Text {
                    width: parent.width
                    text: (root.info.artist || "") + (root.info.year ? "  \u2022  " + root.info.year : "")
                    elide: Text.ElideRight
                    font.pixelSize: 15
                    color: Pal.onSurfaceVariant
                }
                Row {
                    spacing: 8
                    Chip { text: U.plural(root.info.count || 0, "song", "songs") + ", " + U.fmtLong(root.info.durationMs || 0) }
                    Chip { text: root.info.quality || ""; accent: !!root.info.lossless; visible: text !== "" }
                }
                Item { width: 1; height: 6 }
                Row {
                    spacing: 10
                    Pill { icon: "play"; text: "Play"; onClicked: Player.playAlbum(root.row, 0, false) }
                    Pill { icon: "shuffle"; text: "Shuffle"; filled: false; onClicked: Player.playAlbum(root.row, 0, true) }
                }
            }
        }

        delegate: Item {
            id: tr
            width: list.width
            height: 56
            readonly property bool current: Player.title === modelData.title && Player.album === root.info.title
            activeFocusOnTab: true
            Keys.onReturnPressed: Player.playAlbum(root.row, index, false)
            Keys.onSpacePressed: Player.playAlbum(root.row, index, false)

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8; anchors.rightMargin: 8
                anchors.topMargin: 2; anchors.bottomMargin: 2
                radius: 18
                color: tr.current ? Pal.secondaryContainer : (hover.containsMouse ? Pal.surfaceHigh : "transparent")
                border.width: tr.activeFocus ? 2 : 0
                border.color: Pal.primary
            }
            Item {
                x: 16; width: 36; height: parent.height
                Icon { anchors.centerIn: parent; visible: tr.current; name: Player.playing ? "volume_up" : "pause"; size: 18; color: Pal.primary }
                Text {
                    anchors.centerIn: parent
                    visible: !tr.current
                    text: modelData.trackNo > 0 ? modelData.trackNo : index + 1
                    font.pixelSize: 13
                    color: Pal.onSurfaceVariant
                }
            }
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 64
                anchors.right: qual.visible ? qual.left : dur.left
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: modelData.title
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: tr.current ? Pal.onSecondaryContainer : Pal.onSurface
                }
                Text {
                    width: parent.width
                    text: modelData.artist
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    color: Pal.onSurfaceVariant
                    visible: modelData.artist !== root.info.artist
                    height: visible ? implicitHeight : 0
                }
            }
            Chip {
                id: qual
                anchors.right: dur.left
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                visible: list.width > 560
                text: modelData.quality
                accent: modelData.lossless
            }
            Text {
                id: dur
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                horizontalAlignment: Text.AlignRight
                text: U.fmt(modelData.durationMs)
                font.pixelSize: 13
                color: Pal.onSurfaceVariant
            }
            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Player.playAlbum(root.row, index, false)
            }
        }
    }
    ScrollIndicator { view: list; anchors.top: list.top; anchors.bottom: list.bottom; height: undefined }
}
