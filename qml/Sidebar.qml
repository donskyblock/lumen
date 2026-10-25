import QtQuick
import "Util.js" as U

Rectangle {
    id: root
    property int page: 0
    signal pageSelected(int page)

    radius: 28
    color: Pal.surfaceLow
    Behavior on color { ColorAnimation { duration: 500 } }

    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        spacing: 4

        Item {
            width: parent.width
            height: 72
            Rectangle {
                id: logo
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40; radius: 14
                color: Pal.primaryContainer
                Icon { anchors.centerIn: parent; name: "music_note"; size: 24; color: Pal.onPrimaryContainer }
            }
            Text {
                anchors.left: logo.right
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "Lumen"
                font.pixelSize: 24
                font.weight: Font.DemiBold
                color: Pal.onSurface
            }
        }
        NavItem { width: parent.width; icon: "search"; text: "Search"; selected: root.page === 2; onClicked: root.pageSelected(2) }
        NavItem { width: parent.width; icon: "album"; text: "Albums"; selected: root.page === 0; onClicked: root.pageSelected(0) }
        NavItem { width: parent.width; icon: "library_music"; text: "Songs"; selected: root.page === 1; onClicked: root.pageSelected(1) }
        NavItem { width: parent.width; icon: "lyrics"; text: "Lyrics"; selected: root.page === 3; onClicked: root.pageSelected(3) }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 8

        NavItem {
            width: parent.width
            icon: "settings"; text: "Settings"
            selected: root.page === 4
            onClicked: root.pageSelected(4)
        }

        Rectangle {
            width: parent.width
            height: status.implicitHeight + 28
            radius: 20
            color: Pal.surfaceHigh

            Column {
                id: status
                anchors.left: parent.left
                anchors.right: refresh.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                spacing: 2
                Text {
                    width: parent.width
                    text: Library.scanning ? "Scanning\u2026 " + Library.scanned + " files" : U.plural(Library.trackCount, "song", "songs")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: Pal.onSurface
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: Library.folders.length === 0 ? "No folder added" : U.plural(Library.folders.length, "folder", "folders")
                    font.pixelSize: 12
                    color: Pal.onSurfaceVariant
                    elide: Text.ElideRight
                }
            }
            IconButton {
                id: refresh
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                icon: "refresh"
                enabled: !Library.scanning && Library.folders.length > 0
                opacity: enabled ? 1 : 0.4
                onClicked: Library.rescan()
            }
        }
    }
}
