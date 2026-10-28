import QtQuick
import "Util.js" as U

Item {
    id: root
    property string arg: ""            // artist name
    property var page: ({})
    function refresh() { page = Library.artistPage(arg) }
    onArgChanged: refresh()
    Component.onCompleted: refresh()
    Timer { id: later; interval: 300; onTriggered: root.refresh() }
    Connections { target: Library.stats; function onChanged() { later.restart() } }
    Connections { target: Library; function onChanged() { later.restart() } }

    readonly property var popular: page.popular || []

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
                Backdrop { anchors.fill: parent; key: root.page.cover || ""; base: Pal.surfaceLow; strength: 0.7 }
                IconButton { x: 12; y: 6; icon: "arrow_back"; tint: Pal.onSurface; onClicked: Bus.back() }
                Cover { id: avatar; x: 32; y: 58; width: 190; height: 190; circle: true; key: root.page.cover || "" }
                Column {
                    anchors.left: avatar.right; anchors.leftMargin: 28
                    anchors.right: parent.right; anchors.rightMargin: 28
                    anchors.bottom: avatar.bottom
                    spacing: 8
                    Text { text: "Artist"; font.pixelSize: Math.round(13 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurfaceVariant }
                    Text { width: parent.width; text: root.page.name || ""; elide: Text.ElideRight
                           font.pixelSize: Math.round(46 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface }
                    Flow {
                        width: parent.width; spacing: 6
                        Chip { text: U.plural(root.page.albumCount || 0, "album", "albums") }
                        Chip { text: U.plural(root.page.trackCount || 0, "song", "songs") }
                        Chip { text: U.fmtLong(root.page.durationMs || 0) }
                        Chip { visible: (root.page.losslessPct || 0) > 0; text: (root.page.losslessPct || 0) + "% lossless"; accent: (root.page.losslessPct || 0) >= 90 }
                        Repeater { model: (root.page.genres || []).slice(0, 3); delegate: Chip { text: modelData } }
                    }
                    Row {
                        spacing: 10
                        Pill { icon: "play"; text: "Play"; onClicked: Player.playPaths(Library.artistPaths(root.arg), 0) }
                        Pill { icon: "shuffle"; text: "Shuffle"; filled: false; onClicked: Player.playPathsShuffled(Library.artistPaths(root.arg)) }
                        Pill { icon: "cloud"; text: "Full catalog"; filled: false; onClicked: Catalog.openArtistByName(root.arg) }
                        IconButton { anchors.verticalCenter: parent.verticalCenter; icon: "more_vert"
                                     onClicked: { var p = mapToItem(null, 0, 40); Bus.menu("artist", { name: root.arg }, p.x, p.y) } }
                    }
                }
            }

            Text {
                x: 28; height: 52; verticalAlignment: Text.AlignBottom
                visible: root.popular.length > 0
                text: root.page.hasPlays ? "Popular" : "Songs"
                font.pixelSize: Math.round(22 * Pal.fs); font.weight: Font.Bold; color: Pal.onSurface
            }
            Repeater {
                model: root.popular
                delegate: TrackRow {
                    width: col.width
                    index: model.index; path: modelData.path; title: modelData.title; artist: modelData.artist; album: modelData.album
                    cover: modelData.cover; quality: modelData.quality; lossless: modelData.lossless; durationMs: modelData.durationMs
                    showAlbum: true
                    onActivated: Player.playPaths(root.popular.map(function (x) { return x.path }), model.index)
                }
            }
            Pill {
                x: 28
                visible: (root.page.trackCount || 0) > root.popular.length
                height: 40; filled: false; text: "Play all " + (root.page.trackCount || 0) + " songs"
                onClicked: Player.playPaths(Library.artistPaths(root.arg), 0)
            }

            Shelf {
                width: col.width
                title: "Discography"
                model: root.page.albums || []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.year > 0 ? modelData.year : ""; cover: modelData.cover
                        onClicked: Bus.go("album", modelData.key)
                        onPlayClicked: Player.playAlbum(modelData.key)
                        onMenuRequested: (x, y) => Bus.menu("album", { key: modelData.key, artist: modelData.artist }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                title: "Appears on"
                model: root.page.appearsOn || []
                card: Component {
                    MediaCard {
                        title: modelData.title; subtitle: modelData.artist; cover: modelData.cover
                        onClicked: Bus.go("album", modelData.key)
                        onPlayClicked: Player.playAlbum(modelData.key)
                        onMenuRequested: (x, y) => Bus.menu("album", { key: modelData.key, artist: modelData.artist }, x, y)
                    }
                }
            }
            Shelf {
                width: col.width
                circle: true
                title: "Fans also like"
                subtitle: "Matched by genre and by what gets played together"
                model: root.page.related || []
                card: Component {
                    MediaCard {
                        circle: true
                        title: modelData.name
                        subtitle: modelData.count + (modelData.count === 1 ? " song" : " songs")
                        cover: modelData.cover
                        onClicked: Bus.go("artist", modelData.name)
                        onPlayClicked: Player.playPaths(Library.artistPaths(modelData.name), 0)
                        onMenuRequested: (x, y) => Bus.menu("artist", { name: modelData.name }, x, y)
                    }
                }
            }
        }
    }
    ScrollIndicator { view: scroller; anchors.top: scroller.top; anchors.bottom: scroller.bottom; height: undefined }
}
