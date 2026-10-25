import QtQuick
import QtQuick.Window

Window {
    id: win
    visible: true
    width: Ui.intValue("w", 1280)
    height: Ui.intValue("h", 820)
    minimumWidth: 860
    minimumHeight: 560
    title: Player.hasTrack ? Player.title + " \u2013 " + Player.artist : "Lumen"
    color: Pal.surface
    Behavior on color { ColorAnimation { duration: 500 } }

    // 0 albums, 1 songs, 2 search, 3 lyrics, 4 settings
    property int page: Ui.intValue("page", 0)
    property int openAlbum: -1
    property bool panelOpen: Ui.boolValue("panel", true)
    property string searchSeed: ""
    property string toast: ""
    readonly property bool showPanel: panelOpen && width >= 1120 && Player.hasTrack && page !== 3
    readonly property bool hasLibrary: Library.trackCount > 0
    readonly property var titles: ["Albums", "Songs", "Search", "Lyrics", "Settings"]

    function go(p) {
        page = p
        openAlbum = -1
        if (p !== 0 && p !== 1) Library.search = ""
    }

    onPageChanged: Ui.set("page", page)
    onPanelOpenChanged: Ui.set("panel", panelOpen)
    onWidthChanged: geo.restart()
    onHeightChanged: geo.restart()
    Timer { id: geo; interval: 600; onTriggered: { Ui.set("w", win.width); Ui.set("h", win.height) } }
    Timer { id: toastTimer; interval: 3800; onTriggered: win.toast = "" }

    Item {
        id: shell
        anchors.fill: parent
        anchors.margins: 8
        focus: true
        Keys.onSpacePressed: Player.toggle()
        TapHandler { onTapped: shell.forceActiveFocus() }

        Sidebar {
            id: side
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: bar.top
            anchors.bottomMargin: 8
            width: win.width >= 1000 ? 248 : 208
            page: win.page
            onPageSelected: (p) => win.go(p)
        }

        Rectangle {
            id: mainPanel
            anchors.left: side.right
            anchors.leftMargin: 8
            anchors.right: panel.visible ? panel.left : parent.right
            anchors.rightMargin: panel.visible ? 8 : 0
            anchors.top: parent.top
            anchors.bottom: bar.top
            anchors.bottomMargin: 8
            radius: 28
            color: Pal.surfaceLow
            Behavior on color { ColorAnimation { duration: 500 } }
            clip: true

            Item {
                id: topBar
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 84
                visible: win.openAlbum < 0 || win.page !== 0

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.titles[win.page]
                    font.pixelSize: 30
                    font.weight: Font.Bold
                    color: Pal.onSurface
                }
                SearchField {
                    anchors.right: parent.right
                    anchors.rightMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(360, parent.width - 220)
                    visible: win.hasLibrary && (win.page === 0 || win.page === 1)
                    placeholder: win.page === 0 ? "Filter albums and artists" : "Filter songs, artists, albums"
                    text: Library.search
                    onTextChanged: if (text !== Library.search) Library.search = text
                }
            }

            Item {
                id: content
                anchors.top: topBar.visible ? topBar.bottom : parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right

                Loader {
                    id: albums
                    anchors.fill: parent
                    active: win.hasLibrary && win.page === 0 && win.openAlbum < 0
                    source: "AlbumsPage.qml"
                }
                Connections { target: albums.item; function onOpenAlbum(row) { win.openAlbum = row } }
                Loader {
                    anchors.fill: parent
                    active: win.hasLibrary && win.page === 1
                    source: "SongsPage.qml"
                }
                Loader {
                    id: search
                    anchors.fill: parent
                    active: win.page === 2 && win.hasLibrary
                    sourceComponent: SearchPage { seed: win.searchSeed }
                }
                Connections { target: search.item; function onOpenAlbum(row) { win.page = 0; win.openAlbum = row } }
                Loader {
                    anchors.fill: parent
                    active: win.page === 3
                    source: "LyricsPage.qml"
                }
                Loader {
                    anchors.fill: parent
                    active: win.page === 4
                    source: "SettingsPage.qml"
                }
                Loader {
                    anchors.fill: parent
                    active: win.hasLibrary && win.page === 0 && win.openAlbum >= 0
                    sourceComponent: AlbumDetail { row: win.openAlbum; onBack: win.openAlbum = -1 }
                }
                EmptyState {
                    anchors.fill: parent
                    visible: !win.hasLibrary && win.page !== 3 && win.page !== 4
                    scanning: Library.scanning
                    onOpenSettings: win.go(4)
                }
            }
        }

        NowPlayingPanel {
            id: panel
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: bar.top
            anchors.bottomMargin: 8
            width: 332
            visible: win.showPanel
        }

        PlayerBar {
            id: bar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 92
            panelOpen: win.panelOpen
            lyricsOpen: win.page === 3
            onTogglePanel: win.panelOpen = !win.panelOpen
            onToggleLyrics: win.go(win.page === 3 ? 0 : 3)
        }

        // drop a folder anywhere to add it
        DropArea {
            id: drop
            anchors.fill: parent
            onDropped: (d) => {
                var msg = ""
                for (var i = 0; i < d.urls.length; ++i) {
                    var r = Library.addFolder(d.urls[i].toString())
                    if (r !== "") msg = r
                }
                win.toast = msg !== "" ? msg : "Added to your library"
                toastTimer.restart()
            }
            Rectangle {
                anchors.fill: parent
                radius: 28
                visible: drop.containsDrag
                color: Qt.rgba(Pal.surface.r, Pal.surface.g, Pal.surface.b, 0.86)
                border.width: 3
                border.color: Pal.primary
                Text {
                    anchors.centerIn: parent
                    text: "Drop a folder to add it to your library"
                    font.pixelSize: 22; font.weight: Font.DemiBold; color: Pal.onSurface
                }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: bar.top
            anchors.bottomMargin: 16
            width: toastText.implicitWidth + 40
            height: 48
            radius: 24
            color: Pal.primaryContainer
            visible: win.toast !== ""
            Text { id: toastText; anchors.centerIn: parent; text: win.toast; font.pixelSize: 14; color: Pal.onPrimaryContainer }
        }
    }
}
