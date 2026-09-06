pragma Singleton

import QtQuick

/// The design system. QML ships no opinion about spacing, type or color, so
/// every visual value in the app reads a token from here.
QtObject {
    id: theme

    readonly property bool dark: Application.styleHints.colorScheme === Qt.Dark

    // ---- Color -------------------------------------------------------------
    readonly property color bg:          dark ? "#0E1013" : "#FCFCFD"
    readonly property color surface:     dark ? "#16191E" : "#FFFFFF"
    readonly property color surfaceAlt:  dark ? "#1C2026" : "#F6F7F9"
    readonly property color hairline:    dark ? "#22262D" : "#ECEEF1"
    readonly property color border:      dark ? "#2C323B" : "#DFE3E8"

    readonly property color text:        dark ? "#EEF1F5" : "#14161A"
    readonly property color textMuted:   dark ? "#99A1AC" : "#6B7280"
    readonly property color textFaint:   dark ? "#5F6874" : "#9AA1AC"

    readonly property color accent:      dark ? "#6E9BF5" : "#3B72E8"
    readonly property color accentText:  "#FFFFFF"
    readonly property color danger:      dark ? "#F06B6B" : "#DC4B4B"

    /// Tint a calendar color for use as an event background.
    function tint(c, amount) {
        return Qt.alpha(c, amount)
    }

    // ---- Spacing -----------------------------------------------------------
    readonly property int space1: 2
    readonly property int space2: 4
    readonly property int space3: 8
    readonly property int space4: 12
    readonly property int space5: 16
    readonly property int space6: 24
    readonly property int space7: 32

    // ---- Radius ------------------------------------------------------------
    readonly property int radiusSm: 4
    readonly property int radiusMd: 6
    readonly property int radiusLg: 10
    readonly property int radiusXl: 14

    // ---- Type --------------------------------------------------------------
    readonly property string fontFamily: "Inter"
    readonly property int textXs: 11
    readonly property int textSm: 12
    readonly property int textMd: 13
    readonly property int textLg: 15
    readonly property int textXl: 20
    readonly property int text2xl: 28

    // ---- Motion ------------------------------------------------------------
    readonly property int durFast: 120
    readonly property int durMed: 180
    readonly property int durSlow: 260
    readonly property int easing: Easing.OutCubic

    // ---- Grid metrics ------------------------------------------------------
    readonly property int hourHeight: 56
    readonly property int gutterWidth: 64
    readonly property int snapMinutes: 15
}
