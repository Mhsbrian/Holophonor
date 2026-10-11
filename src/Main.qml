import QtQuick
import QtQuick.Controls
import Holophonor

ApplicationWindow {
    id: win
    width: 1280
    height: 800
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: "Holophonor"
    color: style.ground

    property bool helpVisible: false
    readonly property real margin: Math.max(28, Math.min(width, height) * 0.045)

    // One small palette drives every surface. The sheet decides light or
    // dark; the Omarchy theme supplies the accent and the vermilion.
    QtObject {
        id: style
        readonly property bool dark: canvas.dark
        readonly property color ground: dark ? "#0b0c10" : "#f1ebde"
        readonly property color ink: dark ? "#ebe3d1" : "#1b1917"
        readonly property color card: dark ? "#15161c" : "#f8f4eb"
        readonly property color hairline: dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0.1, 0.09, 0.08, 0.1)
        readonly property color shade: dark ? Qt.rgba(0, 0, 0, 0.3) : Qt.rgba(0.22, 0.16, 0.08, 0.05)
        readonly property color accent: theme.accent
        readonly property color vermilion: theme.vermilion
        readonly property string serif: "Noto Serif Display"
        readonly property string mono: "iA Writer Mono S"
    }

    InkCanvas {
        id: canvas
        anchors.fill: parent
        source: backend
        mode: backend.visual
        accent: theme.accent
        vermilion: theme.vermilion
        pigments: theme.pigments
        focus: true

        // The computer keyboard plays when nothing else has focus.
        Keys.onPressed: event => {
            if (!event.isAutoRepeat && backend.keyDown(event.key))
                event.accepted = true;
        }
        Keys.onReleased: event => {
            if (!event.isAutoRepeat && backend.keyUp(event.key))
                event.accepted = true;
        }
        onActiveFocusChanged: if (!activeFocus) backend.releaseKeys()
    }

    InstrumentMenu {
        id: instrumentMenu
        style: style
        x: win.margin
        y: win.margin * 0.8
        onClosed: canvas.forceActiveFocus()
    }

    VisualMenu {
        id: visualMenu
        style: style
        anchors.right: parent.right
        anchors.rightMargin: win.margin
        y: win.margin * 0.8
        onClosed: canvas.forceActiveFocus()
    }

    // An invitation, gone with the first breath.
    Column {
        anchors.centerIn: parent
        spacing: 16
        opacity: backend.hasPlayed ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 1400; easing.type: Easing.InOutQuad } }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Breathe to begin."
            font.family: style.serif
            font.italic: true
            font.weight: Font.Light
            font.pixelSize: 42
            color: style.ink
            opacity: 0.62
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: backend.device !== "" ? backend.device + " is listening"
                                        : "Plug in an EWI — or play the keys  A S D F G H J K"
            font.family: style.mono
            font.pixelSize: 12
            font.letterSpacing: 0.6
            color: style.ink
            opacity: 0.42
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "?  shows every shortcut"
            font.family: style.mono
            font.pixelSize: 11
            color: style.ink
            opacity: 0.26
        }
    }

    // What is sounding, to the cent, and how hard it is breathed.
    Row {
        x: win.margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: win.margin * 0.8
        spacing: 14
        opacity: backend.sounding ? 0.78 : (backend.hasPlayed ? 0.28 : 0)
        Behavior on opacity { NumberAnimation { duration: 500 } }

        Text {
            width: 36
            text: backend.noteName
            font.family: style.mono
            font.pixelSize: 13
            color: style.ink
        }
        Text {
            width: 46
            text: (backend.cents < 0 ? "−" : "+") + String(Math.abs(backend.cents)).padStart(2, "0") + "¢"
            font.family: style.mono
            font.pixelSize: 13
            color: style.ink
            opacity: 0.6
        }
        Item {
            width: 104
            height: 13
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 1
                color: style.ink
                opacity: 0.16
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * backend.level
                height: 2
                color: style.ink
            }
        }
        Text {
            text: backend.breathSource !== "" ? backend.breathSource : "keys · envelope"
            font.family: style.mono
            font.pixelSize: 11
            color: style.ink
            opacity: 0.5
        }
    }

    // The controller, and anything that's wrong, in plain words.
    Column {
        anchors.right: parent.right
        anchors.rightMargin: win.margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: win.margin * 0.8
        spacing: 6

        Text {
            anchors.right: parent.right
            visible: text !== ""
            text: backend.audioError
            font.family: style.mono
            font.pixelSize: 11
            color: style.vermilion
        }
        Text {
            anchors.right: parent.right
            visible: text !== ""
            text: backend.midiError
            font.family: style.mono
            font.pixelSize: 11
            color: style.vermilion
        }
        Row {
            anchors.right: parent.right
            spacing: 9
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 7
                height: 7
                radius: 3.5
                color: backend.device !== "" ? (backend.ewi ? style.accent : style.ink) : "transparent"
                border.width: 1
                border.color: backend.ewi ? style.accent : style.ink
                opacity: backend.device !== "" ? 0.9 : 0.4
                // Breathes slowly while waiting, the way a standby light does.
                SequentialAnimation on opacity {
                    running: backend.device === ""
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.12; duration: 1500; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.45; duration: 1500; easing.type: Easing.InOutSine }
                }
            }
            Text {
                text: backend.device !== "" ? backend.device : "waiting for a controller"
                font.family: style.mono
                font.pixelSize: 11
                color: style.ink
                opacity: 0.55
            }
        }
    }

    Item {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        y: win.margin * 0.9
        width: toastLabel.implicitWidth + 40
        height: 40
        opacity: 0
        visible: opacity > 0

        function show(message) {
            toastLabel.text = message;
            toastAnimation.restart();
        }

        Card {
            anchors.fill: parent
            style: style
        }
        Text {
            id: toastLabel
            anchors.centerIn: parent
            font.family: style.serif
            font.italic: true
            font.pixelSize: 16
            color: style.ink
        }
        SequentialAnimation {
            id: toastAnimation
            NumberAnimation { target: toast; property: "opacity"; to: 1; duration: 180 }
            PauseAnimation { duration: 2400 }
            NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 600 }
        }
    }

    Connections {
        target: backend
        function onToast(message) { toast.show(message) }
    }

    HelpOverlay {
        anchors.fill: parent
        style: style
        shown: win.helpVisible
        onDismissed: win.helpVisible = false
    }

    Shortcut { sequences: ["/", "Ctrl+K"]; onActivated: instrumentMenu.open() }
    Shortcut { sequence: "V"; onActivated: visualMenu.open() }
    Shortcut { sequence: "["; onActivated: backend.stepInstrument(-1) }
    Shortcut { sequence: "]"; onActivated: backend.stepInstrument(1) }
    Shortcut { sequence: "1"; onActivated: backend.visual = 0 }
    Shortcut { sequence: "2"; onActivated: backend.visual = 1 }
    Shortcut { sequence: "3"; onActivated: backend.visual = 2 }
    Shortcut { sequence: "4"; onActivated: backend.visual = 3 }
    Shortcut { sequence: "Space"; onActivated: canvas.freshSheet() }
    Shortcut {
        sequence: "Ctrl+S"
        onActivated: {
            const path = canvas.save();
            toast.show(path !== "" ? "Saved to Pictures/Holophonor/" + path.split("/").pop()
                                   : "Couldn't save the painting");
        }
    }
    Shortcut { sequence: "Ctrl+D"; onActivated: backend.playDemo() }
    Shortcut {
        sequence: "F11"
        onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }
    Shortcut { sequence: "?"; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Esc"
        // Open menus own Escape; two claims on one key would cancel each other.
        enabled: !instrumentMenu.opened && !visualMenu.opened
        onActivated: {
            if (win.helpVisible)
                win.helpVisible = false;
            else
                backend.panic();
        }
    }
}
