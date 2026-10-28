import QtQuick

// Profile ("vibe") editor: everything here belongs to the active profile.
Column {
    id: root
    spacing: 12
    readonly property var p: Profiles.active
    function row(label) { return label }

    Text { text: "Appearance"; font.pixelSize: Math.round(20 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
    Text {
        width: parent.width; wrapMode: Text.WordWrap
        text: "A profile is a saved look: colours, shapes, density, motion and what the home page shows. Switch from the avatar at the top right at any time."
        font.pixelSize: Math.round(14 * Pal.fs); color: Pal.onSurfaceVariant
    }

    Flow {
        width: parent.width; spacing: 8
        Repeater {
            model: Profiles.list
            delegate: FilterChip { text: modelData.name; checked: modelData.id === root.p.id; onClicked: Profiles.select(modelData.id) }
        }
    }
    Row {
        spacing: 10
        Pill { icon: "edit"; text: "Rename"; filled: false; onClicked: Bus.prompt("Rename profile", root.p.name || "", "renameProfile") }
        Pill { icon: "content_copy"; text: "Duplicate"; filled: false; onClicked: Profiles.duplicate(root.p.id) }
        Pill { icon: "delete"; text: "Delete"; filled: false; visible: Profiles.list.length > 1; onClicked: Profiles.remove(root.p.id) }
    }

    Rectangle {
        width: parent.width; height: set.height + 16; radius: 24 * Pal.rs; color: Pal.surfaceHigh
        Column {
            id: set
            x: 20; y: 8; width: parent.width - 40
            spacing: 0

            component Line: Item {
                property string label: ""
                property string hint: ""
                default property alias content: holder.data
                width: set.width; height: Math.max(60, holder.childrenRect.height + 20)
                Column {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.34
                    Text { text: label; font.pixelSize: Math.round(14 * Pal.fs); font.weight: Font.Medium; color: Pal.onSurface }
                    Text { visible: hint !== ""; width: parent.width; wrapMode: Text.WordWrap; text: hint; font.pixelSize: Math.round(12 * Pal.fs); color: Pal.onSurfaceVariant }
                }
                Item { id: holder; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: parent.width * 0.62; height: childrenRect.height }
            }

            Line {
                label: "Theme"
                Segmented { anchors.right: parent.right; value: root.p.scheme
                    options: [{ value: "dark", label: "Dark" }, { value: "amoled", label: "Black" }, { value: "light", label: "Light" }]
                    onPicked: (v) => Profiles.set("scheme", v) }
            }
            Line {
                label: "Accent colour"; hint: "Follow the album art, or pick your own."
                Segmented { anchors.right: parent.right; value: root.p.accent
                    options: [{ value: "cover", label: "From cover art" }, { value: "fixed", label: "Custom" }]
                    onPicked: (v) => Profiles.set("accent", v) }
            }
            Line {
                visible: root.p.accent === "fixed"; height: visible ? 60 : 0; label: "Hue"
                Sld { anchors.right: parent.right; width: parent.width; from: 0; to: 359; rainbow: true; value: root.p.hue
                      onMoved: (v) => Profiles.set("hue", v) }
            }
            Line {
                visible: root.p.accent === "fixed"; height: visible ? 60 : 0; label: "Intensity"
                Sld { anchors.right: parent.right; width: parent.width; from: 5; to: 100; value: root.p.chroma
                      onMoved: (v) => Profiles.set("chroma", v) }
            }
            Line {
                label: "Corners"
                Segmented { anchors.right: parent.right; value: root.p.radius
                    options: [{ value: 0, label: "Sharp" }, { value: 1, label: "Soft" }, { value: 2, label: "Round" }]
                    onPicked: (v) => Profiles.set("radius", v) }
            }
            Line {
                label: "Density"
                Segmented { anchors.right: parent.right; value: root.p.density
                    options: [{ value: 1, label: "Comfortable" }, { value: 0, label: "Compact" }]
                    onPicked: (v) => Profiles.set("density", v) }
            }
            Line {
                label: "Card size"
                Segmented { anchors.right: parent.right; value: root.p.grid
                    options: [{ value: 0, label: "Small" }, { value: 1, label: "Medium" }, { value: 2, label: "Large" }]
                    onPicked: (v) => Profiles.set("grid", v) }
            }
            Line {
                label: "Text size"; hint: root.p.fontScale + "%"
                Sld { anchors.right: parent.right; width: parent.width; from: 85; to: 125; value: root.p.fontScale
                      onMoved: (v) => Profiles.set("fontScale", v) }
            }
            Line {
                label: "Cover glow"; hint: "Soft colour wash behind artwork."
                Sld { anchors.right: parent.right; width: parent.width; from: 0; to: 100; value: root.p.backdrop
                      onMoved: (v) => Profiles.set("backdrop", v) }
            }
            Line {
                label: "Animations"; hint: "Turn off for the calmest, lightest UI."
                Switch { anchors.right: parent.right; checked: !!root.p.motion; onToggled: (on) => Profiles.set("motion", on) }
            }
            Line {
                label: "Wavy seek bar"
                Switch { anchors.right: parent.right; checked: !!root.p.wavy; onToggled: (on) => Profiles.set("wavy", on) }
            }
        }
    }

    Text { text: "Home page sections"; font.pixelSize: Math.round(15 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
    Flow {
        width: parent.width; spacing: 8
        Repeater {
            model: [{ k: "quick", t: "Quick picks" }, { k: "mixes", t: "Made for you" }, { k: "recent", t: "Jump back in" },
                    { k: "added", t: "Recently added" }, { k: "playlists", t: "Playlists" }, { k: "top", t: "On repeat" }, { k: "artists", t: "Artists" }]
            delegate: FilterChip { text: modelData.t; checked: (root.p.homeHidden || []).indexOf(modelData.k) < 0
                                   onClicked: Profiles.toggleHomeSection(modelData.k) }
        }
    }

    Text { text: "Start a new profile from"; font.pixelSize: Math.round(15 * Pal.fs); font.weight: Font.DemiBold; color: Pal.onSurface }
    Flow {
        width: parent.width; spacing: 8
        Repeater {
            model: Profiles.presets
            delegate: FilterChip { icon: "add"; text: modelData.name
                                   onClicked: Bus.prompt("Name your profile", modelData.name, "newProfile", modelData.id) }
        }
    }
}
