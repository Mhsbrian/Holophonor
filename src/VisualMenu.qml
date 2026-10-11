import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Top right: how the music is drawn. The same title treatment as the
// instrument, mirrored; the number keys switch directly.
Item {
    id: root
    property QtObject style
    readonly property bool opened: popup.opened
    signal closed

    readonly property var modes: [
        { name: "Sumi", line: "ink brush · pitch is height, breath is weight" },
        { name: "Bloom", line: "watercolour · harmony chooses the pigment" },
        { name: "Graphite", line: "score line · every note named" },
        { name: "Projection", line: "the Holophonor's light · a harmonograph" }
    ]

    function open() {
        popup.open();
    }

    implicitWidth: header.implicitWidth
    implicitHeight: header.implicitHeight

    Column {
        id: header
        anchors.right: parent.right
        spacing: 3

        Text {
            anchors.right: parent.right
            text: "VISUAL"
            font.family: root.style.mono
            font.pixelSize: 10
            font.letterSpacing: 2.6
            color: root.style.ink
            opacity: 0.42
        }
        Row {
            anchors.right: parent.right
            spacing: 12
            Chevron {
                anchors.verticalCenter: title.verticalCenter
                anchors.verticalCenterOffset: 3
                color: root.style.ink
                open: popup.opened
                opacity: hover.containsMouse || popup.opened ? 0.7 : 0.3
            }
            Text {
                id: title
                text: root.modes[backend.visual].name
                font.family: root.style.serif
                font.weight: Font.Light
                font.pixelSize: 32
                color: root.style.ink
            }
        }
        Rectangle {
            anchors.right: parent.right
            width: hover.containsMouse || popup.opened ? title.width : 0
            height: 1
            color: root.style.ink
            opacity: 0.32
            Behavior on width { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        id: hover
        anchors.fill: header
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.opened ? popup.close() : popup.open()
    }

    Popup {
        id: popup
        x: root.width - width
        y: header.height + 12
        width: 400
        padding: 14
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Card { style: root.style }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 }
            NumberAnimation { property: "y"; from: header.height + 2; to: header.height + 12; duration: 240; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: 120 }
        }

        onOpened: {
            list.currentIndex = backend.visual;
            list.forceActiveFocus();
        }
        onClosed: root.closed()

        function choose(index) {
            backend.visual = index;
            popup.close();
        }

        contentItem: ListView {
            id: list
            implicitHeight: contentHeight
            interactive: false
            model: root.modes
            highlightMoveDuration: 90
            highlight: Rectangle {
                radius: 4
                color: root.style.ink
                opacity: 0.06
            }
            Keys.onReturnPressed: popup.choose(currentIndex)
            Keys.onEnterPressed: popup.choose(currentIndex)
            Keys.onPressed: event => {
                const n = parseInt(event.text);
                if (n >= 1 && n <= root.modes.length) {
                    popup.choose(n - 1);
                    event.accepted = true;
                }
            }

            delegate: Item {
                id: row
                required property int index
                required property var modelData
                width: ListView.view.width
                height: 58

                Text {
                    x: 12
                    y: 12
                    text: row.index + 1
                    font.family: root.style.mono
                    font.pixelSize: 11
                    color: root.style.ink
                    opacity: 0.35
                }
                Rectangle {
                    x: 2
                    y: 18
                    width: 5
                    height: 5
                    radius: 2.5
                    color: root.style.accent
                    visible: row.index === backend.visual
                }
                Column {
                    x: 36
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: row.modelData.name
                        font.family: root.style.serif
                        font.pixelSize: 19
                        font.weight: row.ListView.isCurrentItem ? Font.Normal : Font.Light
                        color: root.style.ink
                    }
                    Text {
                        text: row.modelData.line
                        font.family: root.style.mono
                        font.pixelSize: 10
                        color: root.style.ink
                        opacity: 0.45
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: list.currentIndex = row.index
                    onClicked: popup.choose(row.index)
                }
            }
        }
    }
}
