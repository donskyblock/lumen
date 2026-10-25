import QtQuick
import "Util.js" as U

// One search box for the whole library plus every enabled "source" extension.
Item {
    id: root
    property string seed: ""
    signal openAlbum(int row)

    property var local: ({ songs: [], albums: [] })
    property var remote: []          // [{pluginId, name, state, items, error}]
    property int gen: 0
    readonly property string query: field.text.trim()
    readonly property bool anyLoading: { for (var i = 0; i < remote.length; ++i) if (remote[i].state === "loading") return true; return false }
    readonly property bool nothing: query !== "" && local.songs.length === 0 && local.albums.length === 0 && !anyLoading
                                    && remote.every(function (r) { return r.items.length === 0 })

    onSeedChanged: if (seed !== "") field.text = seed
    Component.onCompleted: field.forceActiveFocus()

    function run() {
        if (query === "") { local = { songs: [], albums: [] }; remote = []; return }
        local = Library.searchLocal(query, 8, 8)
        Plugins.search(query)
    }
    function songPaths() { return local.songs.map(function (s) { return s.path }) }

    Timer { id: debounce; interval: 260; onTriggered: root.run() }
    Connections {
        target: Plugins
        function onSearchStarted(g, ids) {
            root.gen = g
            root.remote = ids.map(function (id) { return { pluginId: id, name: "", state: "loading", items: [], error: "" } })
        }
        function onSearchResult(g, id, name, items, error) {
            if (g !== root.gen) return
            root.remote = root.remote.map(function (r) {
                return r.pluginId === id ? { pluginId: id, name: name, state: error ? "error" : "done", items: items, error: error } : r
            })
        }
    }

    SearchField {
        id: field
        anchors.top: parent.top
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        height: 56
        placeholder: "What do you want to hear?"
        onTextChanged: debounce.restart()
    }

    Flickable {
        id: scroller
        anchors.top: field.bottom
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: width
        contentHeight: col.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: scroller.width
            spacing: 4

            // ---- idle / empty ----
            Item {
                width: parent.width; height: 260
                visible: root.query === ""
                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    width: Math.min(420, parent.width - 48)
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "search"; size: 40; color: Pal.outline }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: "Search your music"
                        font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface
                    }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        text: "Songs, albums and artists from your library. Extensions you turn on in Settings are searched too."
                        font.pixelSize: 14; color: Pal.onSurfaceVariant
                    }
                }
            }
            Item {
                width: parent.width; height: 160
                visible: root.nothing
                Text {
                    anchors.centerIn: parent
                    text: "No results for \u201c" + root.query + "\u201d"
                    font.pixelSize: 16; color: Pal.onSurfaceVariant
                }
            }

            // ---- local songs ----
            Text {
                x: 28; height: 40; verticalAlignment: Text.AlignVCenter
                visible: root.local.songs.length > 0
                text: "Songs"; font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface
            }
            Repeater {
                model: root.local.songs
                delegate: Item {
                    id: sr
                    width: col.width; height: 60
                    readonly property bool current: modelData.path === Player.currentPath
                    activeFocusOnTab: true
                    Keys.onReturnPressed: Player.playPaths(root.songPaths(), index)
                    Rectangle {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; anchors.topMargin: 2; anchors.bottomMargin: 2
                        radius: 18
                        color: sr.current ? Pal.secondaryContainer : (sm.containsMouse ? Pal.surfaceHigh : "transparent")
                        border.width: sr.activeFocus ? 2 : 0; border.color: Pal.primary
                    }
                    Cover { id: sc; x: 20; anchors.verticalCenter: parent.verticalCenter; width: 44; height: 44; key: modelData.cover; radiusPct: 20 }
                    Column {
                        anchors.left: sc.right; anchors.leftMargin: 14
                        anchors.right: sq.visible ? sq.left : st.left; anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: parent.width; text: modelData.title; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.Medium; color: Pal.onSurface }
                        Text { width: parent.width; text: modelData.artist + "  \u2022  " + modelData.album; elide: Text.ElideRight; font.pixelSize: 13; color: Pal.onSurfaceVariant }
                    }
                    Chip { id: sq; anchors.right: st.left; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
                           visible: col.width > 680; text: modelData.quality; accent: modelData.lossless }
                    Text { id: st; anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter
                           width: 48; horizontalAlignment: Text.AlignRight; text: U.fmt(modelData.durationMs); font.pixelSize: 13; color: Pal.onSurfaceVariant }
                    MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: Player.playPaths(root.songPaths(), index) }
                }
            }

            // ---- local albums ----
            Text {
                x: 28; height: 52; verticalAlignment: Text.AlignBottom
                visible: root.local.albums.length > 0
                text: "Albums"; font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface
            }
            Flow {
                x: 20; width: col.width - 40
                visible: root.local.albums.length > 0
                spacing: 8
                Repeater {
                    model: root.local.albums
                    delegate: Item {
                        id: ac
                        width: 164; height: 214
                        activeFocusOnTab: true
                        Keys.onReturnPressed: root.openAlbum(modelData.row)
                        Rectangle { anchors.fill: parent; radius: 22; color: Pal.onSurface; opacity: am.containsMouse ? 0.07 : 0
                                    border.width: ac.activeFocus ? 2 : 0; border.color: Pal.primary }
                        Column {
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Cover { width: parent.width; height: width; key: modelData.cover; radiusPct: 14 }
                            Text { width: parent.width; text: modelData.title; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.DemiBold; color: Pal.onSurface }
                            Text { width: parent.width; text: modelData.artist; elide: Text.ElideRight; font.pixelSize: 13; color: Pal.onSurfaceVariant }
                        }
                        MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openAlbum(modelData.row) }
                    }
                }
            }

            // ---- extensions ----
            Repeater {
                model: root.remote
                delegate: Column {
                    id: rem
                    width: col.width
                    readonly property var res: modelData
                    visible: res.state !== "done" || res.items.length > 0
                    Text {
                        x: 28; height: 52; verticalAlignment: Text.AlignBottom
                        text: "From " + (res.name !== "" ? res.name : "extension")
                        font.pixelSize: 20; font.weight: Font.DemiBold; color: Pal.onSurface
                    }
                    Text {
                        x: 28; height: 36; verticalAlignment: Text.AlignVCenter
                        visible: res.state === "loading"
                        text: "Searching\u2026"; font.pixelSize: 14; color: Pal.onSurfaceVariant
                    }
                    Text {
                        x: 28; width: col.width - 56; height: implicitHeight + 12; wrapMode: Text.WordWrap
                        visible: res.state === "error"
                        text: "This extension couldn't search: " + res.error
                        font.pixelSize: 14; color: "#ffb4ab"
                    }
                    Repeater {
                        model: res.items
                        delegate: Item {
                            id: rr
                            width: col.width; height: 60
                            activeFocusOnTab: true
                            Keys.onReturnPressed: Player.playRemote(res.pluginId, modelData)
                            Rectangle { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; anchors.topMargin: 2; anchors.bottomMargin: 2
                                        radius: 18; color: rm.containsMouse ? Pal.surfaceHigh : "transparent"
                                        border.width: rr.activeFocus ? 2 : 0; border.color: Pal.primary }
                            Cover { id: rc; x: 20; anchors.verticalCenter: parent.verticalCenter; width: 44; height: 44; url: modelData.cover || ""; radiusPct: 20 }
                            Column {
                                anchors.left: rc.right; anchors.leftMargin: 14; anchors.right: rd.left; anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                Text { width: parent.width; text: modelData.title; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.Medium; color: Pal.onSurface }
                                Text { width: parent.width; text: modelData.artist || ""; elide: Text.ElideRight; font.pixelSize: 13; color: Pal.onSurfaceVariant }
                            }
                            Text { id: rd; anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter
                                   width: 48; horizontalAlignment: Text.AlignRight
                                   text: modelData.durationMs > 0 ? U.fmt(modelData.durationMs) : ""; font.pixelSize: 13; color: Pal.onSurfaceVariant }
                            MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: Player.playRemote(res.pluginId, modelData) }
                        }
                    }
                }
            }
        }
    }
    ScrollIndicator { view: scroller; anchors.top: scroller.top; anchors.bottom: scroller.bottom; height: undefined }
}
