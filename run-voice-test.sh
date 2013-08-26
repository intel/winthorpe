#!/bin/bash

VERBOSITY=${VERBOSITY:--vvv}
PLUGINDIR=${PLUGINDIR:-$(pwd)/src/.libs}
CONFIGFILE=${CONFIGFILE:-$(pwd)/voice-test.conf}

plugins="simple-disambiguator sphinx-speech festival-loader festival-voice"
clients="dbus-client"
PULSESRC=""

select_input_device () {
    local _pcidev _usbdev _dev _cnt _i _def _choice=""

    _pcidev=`pactl list sources | grep Name: | tr -s '\t' ' ' |
                sed 's/^ *Name: //g' | grep -v monitor | grep pci`
    _usbdev=`pactl list sources | grep Name: | tr -s '\t' ' ' |
                sed 's/^ *Name: //g' | grep -v monitor | grep usb`

    _cnt=$((`echo $_pcidev | wc -l` + `echo $_usbdev | wc -l`))

    while [ -z "$_choice" ]; do
    echo "Please select an available input device:"
    _i=1

    if [ -n "$_usbdev" ]; then
        echo "    * USB devices:"
        for _dev in $_usbdev; do
            [ "$_i" = "1" ] && _def="(default)" || _def=""
            echo "        $_i: $_dev $_def"
            _i=$(($_i + 1))
        done
    fi

    if [ -n "$_pcidev" ]; then
        echo "    * PCI devices:"
        for _dev in $_pcidev; do
            [ "$_i" = "1" ] && _def="(default)" || _def=""
            echo "        $_i: $_dev $_def"
            _i=$(($_i + 1))
        done
    fi

    echo "    * Preconfigured device:"
    echo "        $_i: from the config file"

    read -p "Please select 1 - $_cnt (default: 1): " _choice
    [ -z "$_choice" ] && _choice=1

    if [ "$_choice" -lt 1 -o "$_choice" -gt $(($_cnt + 1)) ]; then
        _choice=""
    fi
    done

    if [ "$_choice" = "$(($_cnt + 1))" ]; then
        _dev=""
    else
        [ -n "$_usbdev" ] && _t=" " || _t=""
        _dev=`echo "$_usbdev${_t}$_pcidev" | tr ' ' '\n' |
                  head -$_choice | tail -1`
    fi

    PULSESRC="$_dev"
}

select_input_device
if [ -n "$PULSESRC" ]; then
    PULSESRC="-s sphinx.pulsesrc=$PULSESRC"
fi

LOAD_OPTIONS=""
for i in $plugins $clients; do
    LOAD_OPTIONS="$LOAD_OPTIONS -L $i"
done

EXTRA_OPTIONS=""
DEBUG_OPTIONS=""
while [ -n "$1" ]; do
    case $1 in
        --valgrind)
            export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$PUGINDIR"
            opts="${1#--valgrind}"
            case $opts in
                =*) xeq="valgrind ${opts#=}";;
                 *) xeq="valgrind --leak-check=full";;
            esac
            ;;
        -n|--dry-run)
            xeq=echo
            ;;
        -d|--debug)
            DEBUG_OPTIONS="-d '*'"
            ;;
         *)
            EXTRA_OPTIONS="$EXTRA_OPTIONS $1"
            ;;
    esac
    shift
done

$xeq ./src/srs-daemon -f $VERBOSITY $DEBUG_OPTIONS \
    -P $PLUGINDIR -c $CONFIGFILE \
    $LOAD_OPTIONS $PULSESRC $EXTRA_OPTIONS
