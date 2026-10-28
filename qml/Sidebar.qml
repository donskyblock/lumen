import QtQuick
import "Util.js" as U

Rectangle {
    id: root
    property string page: "home"
    signal pageSelected(string page)

    radius: 28 * Pal.rs
    color: Pal.surfaceLow
    Behavior on color { ColorAnimation { duration: Pal.motion ? 500 : 0 } }

    Column {
        id: nav
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        spacing: 2

        Item {
            width: parent.width
            height: 64
            Rectangle {
                id: logo
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 38; height: 38; radius: 13 * Pal.rs
                color: Pal.primaryContainer
                Icon { anchors.centerIn: parent; name: "music_note"; size: 22; color: Pal.onPrimaryContainer }
            }
            Text {
                anchors.left: logo.right
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "Lumen"
                font.pixelSize: Math.round(22 * Pal.fs)
                font.weight: Font.DemiBold
                color: Pal.onSurface
            }
        }
        NavItem { width: parent.width; height: Math.round(46 * Pal.dens); icon: "home"; text: "Home"; selected: root.page === "home"; onClicked: root.pageSelected("home") }
        NavItem { width: parent.width; height: Math.round(46 * Pal.dens); icon: "person"; text: "Artists"; selected: root.page === "artists" || root.page === "artist"; onClicked: root.pageSelected("artists") }
        NavItem { width: parent.width; height: Math.round(46 * Pal.dens); icon: "album"; text: "Albums"; selected: root.page === "albums" || root.page === "album"; onClicked: root.pageSelected("albums") }
        NavItem { width: parent.width; height: Math.round(46 * Pal.dens); icon: "library_music"; text: "Songs"; selected: root.page === "songs"; onClicked: root.pageSelected("songs") }
        NavItem { width: parent.width; height: Math.round(46 * Pal.dens); icon: "lyrics"; text: "Lyrics"; selected: root.page === "lyrics"; onClicked: root.pageSelected("lyrics") }
    }

    // playlists
    Item {
        id: plHead
        anchors.top: nav.bottom
        anchors.topMargin: 10
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 20; anchors.rightMargin: 12
        height: 40
        Text { anchors.verticalCenter: parent.verticalCenter; text: "Playlists"; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant }
        IconButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 36; height: 36
                     icon: "add"; iconSize: 20; onClicked: Bus.prompt("New playlist", "My playlist", "newPlaylist", []) }
    }
    ListView {
        id: pl
        anchors.top: plHead.bottom
        anchors.bottom: bottom.top
        anchors.bottomMargin: 8
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 10; anchors.rightMargin: 10
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: Playlists.list
        delegate: Item {
            id: pr
            width: pl.width; height: Math.round(54 * Pal.dens)
            readonly property bool sel: root.page === "playlist" && Bus.currentArg === modelData.id
            activeFocusOnTab: true
            Keys.onReturnPressed: Bus.go("playlist", modelData.id)
            Rectangle { anchors.fill: parent; radius: 16 * Pal.rs; color: pm.containsMouse ? Pal.surfaceHigh : "transparent"
                        border.width: pr.activeFocus ? 2 : 0; border.color: Pal.primary }
            Item {
                id: pc; x: 8; anchors.verticalCenter: parent.verticalCenter; width: Math.round(40 * Pal.dens); height: width
                Mosaic { anchors.fill: parent; visible: modelData.special !== "liked"; covers: modelData.covers; radiusPct: 18 }
                Rectangle { anchors.fill: parent; visible: modelData.special === "liked"; radius: width * 0.18 * Pal.rs
                            gradient: Gradient { GradientStop { position: 0; color: Pal.primaryContainer } GradientStop { position: 1; color: Pal.primary } }
                            Icon { anchors.centerIn: parent; name: "favorite"; size: 20; color: Pal.onPrimary } }
            }
            Column {
                anchors.left: pc.right; anchors.leftMargin: 12; anchors.right: parent.right; anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                Text { width: parent.width; text: modelData.name; elide: Text.ElideRight; font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                Text { width: parent.width; text: modelData.count + (modelData.count === 1 ? " song" : " songs"); font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant }
            }
            MouseArea {
                id: pm; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton; cursorShape: Qt.PointingHandCursor
                onClicked: (m) => {
                    if (m.button === Qt.RightButton) { var p = pr.mapToItem(null, m.x, m.y); Bus.menu("playlist", { id: modelData.id, special: modelData.special }, p.x, p.y) }
                    else Bus.go("playlist", modelData.id)
                }
            }
        }
    }

    Column {
        id: bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 8

        NavItem {
            width: parent.width
            height: Math.round(46 * Pal.dens)
            icon: "settings"; text: "Settings"
            selected: root.page === "settings"
            onClicked: root.pageSelected("settings")
        }
        Rectangle {
            width: parent.width
            height: status.implicitHeight + 24
            radius: 20 * Pal.rs
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
                    font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface; elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: Library.folders.length === 0 ? "No folder added" : U.plural(Library.folders.length, "folder", "folders")
                    font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant; elide: Text.ElideRight
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
