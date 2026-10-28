import QtQuick
import "Util.js" as U

// An artist from the online catalog: popular songs, discography, related artists.
// Songs play straight away (a full-length source if you have one, otherwise the catalog's preview).
Item {
    id: root
    property string arg: ""            // catalog artist id
    property var page: ({})
    property string error: ""
    property bool loading: true
    property bool showAll: false
    readonly property bool saved: Catalog.savedRevision >= 0 && root.arg !== "" && Catalog.isSavedArtist(root.arg)
    readonly property bool importingThis: Catalog.importing === root.arg && root.arg !== ""
    readonly property var popular: page.popular || []
    readonly property var shownPopular: showAll ? popular : popular.slice(0, 5)
    function paths() { return popular.map(function (x) { return x.path }) }

    function load() { page = ({}); error = ""; loading = true; showAll = false; if (arg !== "") Catalog.loadArtist(arg) }
    onArgChanged: load()
    Component.onCompleted: load()
    Connections {
        target: Catalog
        function onArtistLoaded(id, p) { if (id === root.arg) { root.page = p; root.loading = false } }
        function onLoadFailed(what, id, err) { if (what === "artist" && id === root.arg) { root.error = err; root.loading = false } }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.height + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: scroller.width

            Item {
                width: parent.width
                height: 290
                Backdrop { anchors.fill: parent; key: root.page.picture || ""; base: Pal.surfaceLow; strength: 0.7 }
                IconButton { x: 12; y: 6; icon: "arrow_back"; tint: Pal.onSurface; onClicked: Bus.back() }
                Cover { id: avatar; x: 32; y: 58; width: 190; height: 190; circle: true; key: root.page.picture || "" }
                Column {
                    anchors.left: avatar.right; anchors.leftMargin: 28
                    anchors.right: parent.right; anchors.rightMargin: 28
                    anchors.bottom: avatar.bottom
                    spacing: 8
                    Text { text: "Artist"; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant }
                    Text { width: parent.width; text: root.page.name || (root.loading ? "Loading…" : ""); elide: Text.ElideRight
                           font.pixelSize: Math.round(46 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                    Flow {
                        width: parent.width; spacing: 6
                        Chip { visible: (root.page.fans || 0) > 0; text: Number(root.page.fans || 0).toLocaleString(Qt.locale(), "f", 0) + " fans" }
                        Chip { visible: (root.page.albums || []).length > 0; text: U.plural((root.page.albums || []).length, "album", "albums") }
                        Chip { visible: root.saved; accent: true; text: "In your library" }
                        Chip { visible: !Resolver.hasFullSource && !root.loading; text: "30 s previews" }
                    }
                    Row {
                        spacing: 10
                        Pill { icon: "play"; text: "Play"; enabled: root.popular.length > 0; onClicked: Player.playPaths(root.paths(), 0) }
                        Pill { icon: "shuffle"; text: "Shuffle"; filled: false; enabled: root.popular.length > 0; onClicked: Player.playPathsShuffled(root.paths()) }
                        Pill { icon: "refresh"; text: "Radio"; filled: false; enabled: root.arg !== ""; onClicked: Taste.startRadio("artist:" + root.arg) }
                        Pill {
                            icon: root.saved ? "check" : "add"
                            text: root.importingThis ? "Adding… " + Math.round(Catalog.importProgress * 100) + "%" : (root.saved ? "In library" : "Add to library")
                            filled: false
                            enabled: !root.importingThis && root.arg !== ""
                            onClicked: root.saved ? Catalog.removeArtist(root.arg) : Catalog.importArtist(root.arg)
                        }
                    }
                }
            }

            Text {
                x: 28; width: parent.width - 56; wrapMode: Text.WordWrap
                visible: root.error !== ""
                height: visible ? implicitHeight + 24 : 0
                text: root.error; font.pixelSize: Math.round(15 * Pal.fs); color: Pal.error
            }

            Text {
                x: 28; height: 52; verticalAlignment: Text.AlignBottom
                visible: root.popular.length > 0
                text: "Popular"
                font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface
            }
            Repeater {
                model: root.shownPopular
                delegate: TrackRow {
                    width: col.width
                    index: model.index; path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
                    cover: modelData.cover; quality: ""; lossless: false; durationMs: modelData.durationMs
                    showAlbum: true
                    onActivated: Player.playPaths(root.paths(), model.index)
                }
            }
            Pill {
                x: 28
                visible: root.popular.length > 5
                height: 40; filled: false; text: root.showAll ? "Show fewer" : "Show all " + root.popular.length
                onClicked: root.showAll = !root.showAll
            }

            Shelf {
                width: col.width
                title: "Albums"
                model: root.page.albums || []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.year > 0 ? modelData.year : ""; cover: modelData.cover
                        onClicked: Bus.go("oalbum", modelData.id)
                        onPlayClicked: Bus.playOnline("album", modelData.id)
                        onMenuRequested: (x, y) => Bus.menu("oalbum", { id: modelData.id, artistId: root.arg }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                title: "Singles and EPs"
                model: root.page.singles || []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.year > 0 ? modelData.year : ""; cover: modelData.cover
                        onClicked: Bus.go("oalbum", modelData.id)
                        onPlayClicked: Bus.playOnline("album", modelData.id)
                        onMenuRequested: (x, y) => Bus.menu("oalbum", { id: modelData.id, artistId: root.arg }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                title: "Compilations"
                model: root.page.compilations || []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.year > 0 ? modelData.year : ""; cover: modelData.cover
                        onClicked: Bus.go("oalbum", modelData.id)
                        onPlayClicked: Bus.playOnline("album", modelData.id)
                        onMenuRequested: (x, y) => Bus.menu("oalbum", { id: modelData.id, artistId: root.arg }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                circle: true
                title: "Fans also like"
                model: root.page.related || []
                card: Component {
                    MediaCard {
                        circle: true
                        title: modelData.name; subtitle: ""; cover: modelData.picture
                        onClicked: Bus.go("oartist", modelData.id)
                        onPlayClicked: Bus.playOnline("artist", modelData.id)
                        onMenuRequested: (x, y) => Bus.menu("oartist", { id: modelData.id, name: modelData.name }, x, y)
                    }
                }
            }
        }
    }
    ScrollIndicator { view: scroller; anchors.top: scroller.top; anchors.bottom: scroller.bottom; height: undefined }
}
