import QtQuick
import QtQuick.Window
import QtQuick.Controls
import "."

Window {
    id: win
    visible: true
    width: Ui.intValue("w", 1280)
    height: Ui.intValue("h", 820)
    minimumWidth: 900
    minimumHeight: 580
    title: Player.hasTrack ? Player.title + " \u2013 " + Player.artist : "Lumen"
    color: Pal.surface
    Behavior on color { ColorAnimation { duration: Pal.motion ? 500 : 0 } }

    // ---- navigation: a route is {page, arg}, with back/forward history ----
    property var route: ({ page: Ui.stringValue("page", "home"), arg: "" })
    property var hist: [route]
    property int hpos: 0
    property string query: ""
    property string toast: ""
    property bool panelOpen: Ui.boolValue("panel", true)
    readonly property bool searching: query.trim() !== ""
    readonly property bool hasLibrary: Library.trackCount > 0
    readonly property bool showPanel: panelOpen && width >= 1160 && Player.hasTrack && route.page !== "lyrics" && !searching
    readonly property var topLevel: ["home", "artists", "albums", "songs", "lyrics", "settings"]
    readonly property var needsLibrary: ["albums", "songs", "artists", "album", "artist"]
    readonly property var pageFiles: ({ home: "HomePage.qml", artists: "ArtistsPage.qml", albums: "AlbumsPage.qml", songs: "SongsPage.qml",
                                        lyrics: "LyricsPage.qml", settings: "SettingsPage.qml", album: "AlbumDetail.qml",
                                        artist: "ArtistPage.qml", playlist: "PlaylistPage.qml", mix: "PlaylistPage.qml",
                                        oartist: "OnlineArtistPage.qml", oalbum: "OnlineAlbumPage.qml" })

    function goRoute(page, arg) {
        var r = { page: page, arg: (arg === undefined || arg === null) ? "" : String(arg) }
        query = ""
        if (route.page === r.page && route.arg === r.arg) return
        hist = hist.slice(0, hpos + 1).concat([r])
        hpos = hist.length - 1
        route = r
    }
    function goBack() { if (hpos > 0) { hpos -= 1; route = hist[hpos]; query = "" } }
    function goForward() { if (hpos < hist.length - 1) { hpos += 1; route = hist[hpos]; query = "" } }
    onRouteChanged: {
        Bus.currentArg = route.arg
        if (topLevel.indexOf(route.page) >= 0) Ui.set("page", route.page)
    }
    onPanelOpenChanged: Ui.set("panel", panelOpen)
    onWidthChanged: geo.restart()
    onHeightChanged: geo.restart()
    Timer { id: geo; interval: 600; onTriggered: { Ui.set("w", win.width); Ui.set("h", win.height) } }
    Timer { id: toastTimer; interval: 3200; onTriggered: win.toast = "" }
    function say(msg) { toast = msg; toastTimer.restart() }

    Connections {
        target: Bus
        function onNavigate(page, arg) { win.goRoute(page, arg) }
        function onNavigateBack() { win.goBack() }
        function onNavigateForward() { win.goForward() }
        function onToastRequested(msg) { win.say(msg) }
        function onMenuRequested(kind, ctx, x, y) { win.showMenu(kind, ctx, x - 8, y - 8) }
        function onPickRequested(paths) { picker.open(paths) }
        function onPlayOnlineRequested(kind, id) {
            if (kind === "album") { win.pendingAlbum = id; Catalog.loadAlbum(id) }
            else if (kind === "artist") { win.pendingArtist = id; Catalog.loadArtist(id) }
            else if (kind === "radio") Taste.startRadio("artist:" + id)
        }
        function onPromptRequested(title, initial, action, arg) { prompt.ask(title, initial, action, arg) }
    }

    // ---- online catalog: things that need loading before they can play ----
    property string pendingAlbum: ""
    property string pendingArtist: ""
    Connections {
        target: Catalog
        function onAlbumLoaded(id, page) { if (id === win.pendingAlbum) { win.pendingAlbum = ""; Player.playPaths(Catalog.albumPaths(id), 0) } }
        function onArtistLoaded(id, page) { if (id === win.pendingArtist) { win.pendingArtist = ""; Player.playPaths(Catalog.artistPaths(id), 0) } }
        function onLoadFailed(what, id, err) { if (id === win.pendingAlbum || id === win.pendingArtist) { win.pendingAlbum = ""; win.pendingArtist = ""; win.say(err) } }
        function onNavigateArtist(id) { win.goRoute("oartist", id) }
        function onMessage(text) { win.say(text) }
    }
    Connections {
        target: Taste
        function onRadioReady(title, paths) { Player.playPaths(paths, 0); win.say("Playing " + title) }
        function onRadioFailed(why) { win.say(why) }
        function onImportFinished(summary) { win.say(summary) }
        function onImportFailed(why) { win.say(why) }
    }

    // ---- context menus ----
    function pathsFor(kind, ctx) {
        if (kind === "track") return [ctx.path]
        if (kind === "album") return Library.albumPaths(ctx.key)
        if (kind === "artist") return Library.artistPaths(ctx.name)
        if (kind === "playlist") return Playlists.playablePaths(ctx.id)
        if (kind === "mix") return Library.mixPaths(ctx.id)
        return []
    }
    property string menuKind: ""
    function showMenu(kind, ctx, x, y) {
        menuKind = kind
        var it = []
        if (kind === "oartist") {
            var sv = Catalog.savedRevision >= 0 && Catalog.isSavedArtist(ctx.id)
            it.push({ id: "oplay", label: "Play", icon: "play" })
            it.push({ id: "radio", label: "Start radio", icon: "shuffle" })
            it.push({ id: "oopen", label: "Open artist", icon: "person" })
            it.push({ sep: true })
            it.push({ id: sv ? "removeArtist" : "importArtist", label: sv ? "Remove from library" : "Add to library", icon: sv ? "delete" : "add", danger: sv })
            menu.items = it; menu.ctx = ctx; menu.popup(x, y)
            return
        }
        if (kind === "oalbum") {
            it.push({ id: "oplay", label: "Play", icon: "play" })
            it.push({ id: "importAlbum", label: "Add to library", icon: "add" })
            if (ctx.artistId) { it.push({ sep: true }); it.push({ id: "oopen", label: "Go to artist", icon: "person" }) }
            menu.items = it; menu.ctx = ctx; menu.popup(x, y)
            return
        }
        if (kind !== "playlist" || (ctx.special !== undefined)) {
            // playable things
        }
        it.push({ id: "play", label: "Play", icon: "play" })
        if (kind !== "track") it.push({ id: "shuffle", label: "Shuffle", icon: "shuffle" })
        if (kind !== "playlist" && kind !== "mix") it.push({ id: "next", label: "Play next", icon: "skip_next" })
        it.push({ id: "queue", label: "Add to queue", icon: "queue_music" })
        if (kind === "track" || kind === "album" || kind === "artist") it.push({ id: "addpl", label: "Add to playlist\u2026", icon: "playlist_add" })
        if (kind === "track") {
            it.push({ id: "like", label: Playlists.isLiked(ctx.path) ? "Remove from Liked songs" : "Add to Liked songs", icon: Playlists.isLiked(ctx.path) ? "favorite" : "favorite_border" })
            if (Library.trackInfo(ctx.path).catalog) {
                var inLib = Catalog.isSaved(ctx.path)
                it.push({ id: inLib ? "removeTrack" : "importTrack", label: inLib ? "Remove from library" : "Add to library", icon: inLib ? "delete" : "add" })
            }
            it.push({ sep: true })
            it.push({ id: "goArtist", label: "Go to artist", icon: "person" })
            it.push({ id: "goAlbum", label: "Go to album", icon: "album" })
            if (ctx.playlistId !== undefined) {
                it.push({ sep: true })
                if (ctx.index > 0) it.push({ id: "up", label: "Move up", icon: "arrow_upward" })
                if (ctx.index < ctx.count - 1) it.push({ id: "down", label: "Move down", icon: "arrow_downward" })
                it.push({ id: "removeFrom", label: "Remove from this playlist", icon: "delete", danger: true })
            }
        }
        if (kind === "album" && ctx.artist) { it.push({ sep: true }); it.push({ id: "goArtistName", label: "Go to artist", icon: "person" }) }
        if (kind === "playlist" && ctx.special !== "liked") {
            it.push({ sep: true })
            it.push({ id: "rename", label: "Rename", icon: "edit" })
            it.push({ id: "delete", label: "Delete playlist", icon: "delete", danger: true })
        }
        if (kind === "mix") { it.push({ sep: true }); it.push({ id: "saveMix", label: "Save as playlist", icon: "playlist_add" }) }
        menu.items = it
        menu.ctx = ctx
        menu.popup(x, y)
    }
    function menuAction(id) {
        var ctx = menu.ctx, kind = win.menuKind
        var paths = pathsFor(kind, ctx)
        switch (id) {
        case "play": Player.playPaths(paths, 0); break
        case "shuffle": Player.playPathsShuffled(paths); break
        case "next": Player.playNext(paths); say("Playing next"); break
        case "queue": Player.addToQueue(paths); say("Added to queue"); break
        case "addpl": picker.open(paths); break
        case "like": Playlists.toggleLike(ctx.path); break
        case "goArtist": { var ti = Library.trackInfo(ctx.path); if (ti.catalog && ti.artistId) goRoute("oartist", ti.artistId); else if (ti.artistName) goRoute("artist", ti.artistName); break }
        case "goAlbum": { var tj = Library.trackInfo(ctx.path); if (tj.catalog && tj.albumId) goRoute("oalbum", tj.albumId); else if (tj.albumKey) goRoute("album", tj.albumKey); break }
        case "oplay": if (kind === "oartist") Bus.playOnline("artist", ctx.id); else Bus.playOnline("album", ctx.id); break
        case "radio": Taste.startRadio("artist:" + ctx.id); break
        case "oopen": goRoute(kind === "oartist" ? "oartist" : "oartist", kind === "oartist" ? ctx.id : ctx.artistId); break
        case "importArtist": Catalog.importArtist(ctx.id); say("Adding to your library\u2026"); break
        case "removeArtist": Catalog.removeArtist(ctx.id); break
        case "importAlbum": Catalog.importAlbum(ctx.id); break
        case "importTrack": Catalog.importTracks([ctx.path]); break
        case "removeTrack": Catalog.removeTracks([ctx.path]); say("Removed from your library"); break
        case "goArtistName": goRoute("artist", String(ctx.artist).split(/ feat\.? | ft\.? |;|\//i)[0].trim()); break
        case "up": Playlists.move(ctx.playlistId, ctx.index, ctx.index - 1); break
        case "down": Playlists.move(ctx.playlistId, ctx.index, ctx.index + 1); break
        case "removeFrom": Playlists.removeAt(ctx.playlistId, ctx.index); break
        case "rename": prompt.ask("Rename playlist", (Playlists.info(ctx.id).name || ""), "renamePlaylist", ctx.id); break
        case "delete": Playlists.remove(ctx.id); if (route.page === "playlist" && route.arg === ctx.id) goBack(); break
        case "saveMix": prompt.ask("Save playlist", Library.mixInfo(ctx.id).title || "Mix", "newPlaylist", paths); break
        }
    }
    function promptDone(action, text, arg) {
        if (action === "newPlaylist") {
            var id = Playlists.create(text, arg || [])
            say("Created \u201C" + (text.trim() || "New playlist") + "\u201D")
            if (!arg || arg.length === 0) goRoute("playlist", id)
        } else if (action === "renamePlaylist") Playlists.rename(arg, text)
        else if (action === "renameProfile") Profiles.set("name", text)
        else if (action === "newProfile") Profiles.create(text, arg)
    }

    Item {
        id: shell
        anchors.fill: parent
        anchors.margins: 8
        focus: true
        Keys.onSpacePressed: Player.toggle()
        Keys.onPressed: (e) => {
            if (e.text === "/") { topSearch.focusInput(); e.accepted = true }
        }
        TapHandler { onTapped: shell.forceActiveFocus() }
        Shortcut { sequences: ["Ctrl+K", "Ctrl+L"]; onActivated: topSearch.focusInput() }
        Shortcut { sequence: "Alt+Left"; onActivated: win.goBack() }
        Shortcut { sequence: "Alt+Right"; onActivated: win.goForward() }
        Shortcut { sequence: "Ctrl+Right"; onActivated: Player.next() }
        Shortcut { sequence: "Ctrl+Left"; onActivated: Player.previous() }

        Sidebar {
            id: side
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: bar.top
            anchors.bottomMargin: 8
            width: win.width >= 1000 ? 252 : 214
            page: win.searching ? "" : win.route.page
            onPageSelected: (p) => win.goRoute(p, "")
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
            radius: 28 * Pal.rs
            color: Pal.surfaceLow
            Behavior on color { ColorAnimation { duration: Pal.motion ? 500 : 0 } }
            clip: true

            // ---- constant top bar: history, search, profile ----
            Item {
                id: topBar
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 76
                z: 20

                Row {
                    id: arrows
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    IconButton { icon: "navigate_before"; tint: Pal.onSurface; enabled: win.hpos > 0; opacity: enabled ? 1 : 0.35; onClicked: win.goBack() }
                    IconButton { icon: "navigate_next"; tint: Pal.onSurface; enabled: win.hpos < win.hist.length - 1; opacity: enabled ? 1 : 0.35; onClicked: win.goForward() }
                }
                SearchField {
                    id: topSearch
                    anchors.left: arrows.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    height: 48
                    width: Math.max(220, Math.min(560, parent.width - arrows.width - avatar.width - 90))
                    placeholder: "Search songs, albums, artists and playlists   ( / )"
                    text: win.query
                    onTextChanged: if (text !== win.query) win.query = text
                }
                Rectangle {
                    id: avatar
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 44; radius: 22 * Pal.rs
                    color: Pal.primaryContainer
                    border.width: avatarMouse.containsMouse || profileMenu.visible ? 2 : 0
                    border.color: Pal.primary
                    activeFocusOnTab: true
                    Keys.onSpacePressed: profileMenu.visible = !profileMenu.visible
                    Accessible.role: Accessible.Button
                    Accessible.name: "Profile and appearance"
                    Text { anchors.centerIn: parent; text: (Profiles.active.name || "?").charAt(0).toUpperCase()
                           font.pixelSize: Math.round(18 * Pal.fs); font.weight: Font.Bold; color: Pal.onPrimaryContainer }
                    MouseArea { id: avatarMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: profileMenu.visible = !profileMenu.visible }
                }
            }

            Item {
                id: content
                anchors.top: topBar.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right

                Loader {
                    id: page
                    anchors.fill: parent
                    source: win.pageFiles[win.route.page] || "HomePage.qml"
                    active: !(win.needsLibrary.indexOf(win.route.page) >= 0 && !win.hasLibrary)
                    onLoaded: sync()
                    function sync() {
                        if (!item) return
                        item.width = Qt.binding(function () { return page.width })     // don't rely on Loader auto-sizing at start-up
                        item.height = Qt.binding(function () { return page.height })
                        if ("arg" in item) item.arg = win.route.arg
                        if ("kind" in item) item.kind = win.route.page
                    }
                    Connections { target: win; function onRouteChanged() { page.sync() } }
                }
                EmptyState {
                    anchors.fill: parent
                    visible: !win.hasLibrary && win.needsLibrary.indexOf(win.route.page) >= 0
                    scanning: Library.scanning
                    onOpenSettings: win.goRoute("settings", "")
                }
                Loader {
                    id: results
                    anchors.fill: parent
                    active: win.searching
                    sourceComponent: Rectangle {
                        color: Pal.surfaceLow
                        property alias query: sp.query
                        SearchPage { id: sp; anchors.fill: parent; query: win.query }
                    }
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
            lyricsOpen: win.route.page === "lyrics"
            onTogglePanel: win.panelOpen = !win.panelOpen
            onToggleLyrics: win.route.page === "lyrics" ? win.goBack() : win.goRoute("lyrics", "")
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
                win.say(msg !== "" ? msg : "Added to your library")
            }
            Rectangle {
                anchors.fill: parent
                radius: 28 * Pal.rs
                visible: drop.containsDrag
                color: Qt.rgba(Pal.surface.r, Pal.surface.g, Pal.surface.b, 0.86)
                border.width: 3
                border.color: Pal.primary
                Text { anchors.centerIn: parent; text: "Drop a folder to add it to your library"
                       font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
            }
        }

        // profile switcher
        MouseArea { anchors.fill: parent; visible: profileMenu.visible; onPressed: profileMenu.visible = false; z: 150 }
        Rectangle {
            id: profileMenu
            visible: false
            z: 160
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 66
            anchors.rightMargin: 12
            width: 280
            height: pcol.height + 16
            radius: 22 * Pal.rs
            color: Pal.surfaceHighest
            border.width: 1; border.color: Pal.outlineVariant
            Column {
                id: pcol
                x: 8; y: 8; width: parent.width - 16
                Text { x: 12; height: 34; verticalAlignment: Text.AlignVCenter; text: "Profiles"; font.pixelSize: Math.round(12 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant }
                Repeater {
                    model: Profiles.list
                    delegate: Item {
                        width: pcol.width; height: 46
                        readonly property bool on: modelData.id === Profiles.active.id
                        Rectangle { anchors.fill: parent; radius: 14 * Pal.rs; color: on ? Pal.secondaryContainer : (pmm.containsMouse ? Pal.surfaceHigh : "transparent") }
                        Rectangle {
                            id: dot; x: 10; anchors.verticalCenter: parent.verticalCenter; width: 28; height: 28; radius: 14 * Pal.rs
                            color: modelData.accent === "fixed" ? Qt.hsla(modelData.hue / 360, 0.5 + modelData.chroma / 250, 0.6, 1) : Pal.primary
                            Text { anchors.centerIn: parent; text: modelData.name.charAt(0).toUpperCase(); font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.Bold; color: "#101010" }
                        }
                        Text { anchors.left: dot.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: modelData.name
                               font.pixelSize: Math.round(14 * Pal.fs); font.weight: on ? Font.DemiBold : Font.Normal; color: on ? Pal.onSecondaryContainer : Pal.onSurface }
                        Icon { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; visible: on; name: "check"; size: 18; color: Pal.onSecondaryContainer }
                        MouseArea { id: pmm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: { Profiles.select(modelData.id); profileMenu.visible = false } }
                    }
                }
                Rectangle { x: 8; width: parent.width - 16; height: 1; color: Pal.outlineVariant }
                Item {
                    width: pcol.width; height: 46
                    Rectangle { anchors.fill: parent; radius: 14 * Pal.rs; color: Pal.onSurface; opacity: cm.containsMouse ? 0.08 : 0 }
                    Icon { x: 14; anchors.verticalCenter: parent.verticalCenter; name: "palette"; size: 20; color: Pal.onSurfaceVariant }
                    Text { x: 46; anchors.verticalCenter: parent.verticalCenter; text: "Customize appearance"; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurface }
                    MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { profileMenu.visible = false; win.goRoute("settings", "") } }
                }
            }
        }

        ContextMenu { id: menu; anchors.fill: parent; onChosen: (id) => win.menuAction(id) }
        PlaylistPicker { id: picker; anchors.fill: parent; onNewPlaylist: (p) => prompt.ask("New playlist", "My playlist", "newPlaylist", p) }
        PromptDialog { id: prompt; anchors.fill: parent; onAccepted: (a, t, arg) => win.promptDone(a, t, arg) }

        Popup {
            id: onboarding
            anchors.centerIn: parent
            width: 500
            modal: true
            focus: true
            closePolicy: Popup.NoAutoClose
            onOpened: onboardingMessage = ""
            property string onboardingMessage: ""
            Component.onCompleted: if (Plugins.onboardingPending) open()
            background: Rectangle { radius: 24 * Pal.rs; color: Pal.surfaceHigh; border.width: 1; border.color: Pal.outlineVariant }
            contentItem: Item {
                implicitWidth: 500
                implicitHeight: onboardingContent.implicitHeight + 48
                Column {
                    id: onboardingContent
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14
                    Text { text: "Welcome to Lumen"; font.pixelSize: Math.round(24 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Set up the official extensions included with this copy of Lumen. They add lyrics and optional Discord Rich Presence. Their files will be copied from this repository into your user extensions folder."
                        font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
                    }
                    Text { visible: onboarding.onboardingMessage !== ""; text: onboarding.onboardingMessage; wrapMode: Text.WordWrap; color: Pal.onSurface; font.pixelSize: Math.round(13 * Pal.fs) }
                    Row {
                        spacing: 10
                        Item { width: 1; height: 42 }
                        Pill { text: "Skip for now"; filled: false; onClicked: { Plugins.finishOnboarding(); onboarding.close() } }
                        Pill { text: "Install official extensions"; filled: true; onClicked: { onboarding.onboardingMessage = Plugins.installOfficialExtensions(); win.say(onboarding.onboardingMessage); onboarding.close() } }
                    }
                }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: bar.top
            anchors.bottomMargin: 16
            width: toastText.implicitWidth + 40
            height: 48
            radius: height * Pal.pf
            color: Pal.primaryContainer
            visible: win.toast !== ""
            z: 400
            Text { id: toastText; anchors.centerIn: parent; text: win.toast; font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onPrimaryContainer }
        }
    }
}
