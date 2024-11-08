# Settings library

This defines the *new* settings library. For the time being `CONFIG_REVK_OLD_SETTINGS` allows the old library which is deprecated.

The purpose of library is to manage *settings*, i.e. non volatile stored parameters and configuration for an application.  These are available in the C code as variables, and can be accessed and changed via MQTT.

## Generating settings

The settings are defined in one or more files, typically `settings.def`, and built using a tool `revk_settings` in to `settings.c` and `settings.h`. The tool is normally in `components/ESP32-RevK/` and built from `revk_settings.c`.

The make process needs to run `revk_settings` on the `.def` files to make `settings.c` and `settings.h`. Include `components/ESP32-RevK/settings.def` in the list of `.def` files. The command only processes files end ing `.def` so you can use `$^` in make for all the dependencies including `revk_setting` itself.

e.g.

```
settings.h:     components/ESP32-RevK/revk_settings settings.def components/ESP32-RevK/settings.def
        components/ESP32-RevK/revk_settings $^
```

The application build needs to include `settings.c` which defines the actual variables. You may want `settings.c` and `settings.h` in `.gitignore`.

The C code including `revk.h` will include `settings.h`.

The settings are loaded in to the C variables when `revk_boot()` is called.

## Settings definitions

The settings definitions file consist of a line per setting, but can also have blank lines and lines starting `//` as a comment. The comment is shown on the web settings page.

It can also include any lines starting with `#`. This is to allow `#ifdef CONFIG_`... Such lines are included in the output files in the appropriate place to allow conditional settings.
This can also be used for `#define` which are then referenced in settings

Each setting in the file has:-

- The setting type, followed by whitespace. E.g. `gpio` or `u8`.
- The setting name, followed by whitespace. E.g. `debug` (cannot start `_` and must fit in NVS, with an extra character to spare for array types).
- Optional default value for setting.
- Optional additional attributes.
- Optional comment (starting `//`) which is used as description for web settings.

The setting types are defined below.

The setting name is the name of the setting as seen in C code, e.g. `baud`, and used in JSON over MQTT. The name can also contain a `.`, e.g. `uart.tx`, to allow grouping in an object in JSON, but in the C code there is no `.`, e.g. `uarttx`.

The default value for a numeric setting can just be a number, e.g. `123`. For a string it can be in quotes, e.g. `"ota.rev.uk"`. The quotes can be omitted for simple text with no spaces and not starting with a `.`.
The default can also be (unquoted) a `CONFIG_...` reference.
Where not specified the defaults for all strings are an empty string `""` (not NULL), and values are all `0`.

Additional attributes are in the form of C structure initialised values, e.g. `.array=10`. These can be separated by commas or spaces.

Note the basic syntax of the definition files are checked, and some invalid combinations reported, but not whether all the attributes are correct, they will be reported when compiling `settings.c`.

## Data types

|Type|Meaning|
|----|-------|
|`bit`|A single bit value holding `0` or `1` (also `false` or `true` in JSON). This is implemented as a bit field in C and a `#define` to allow access by name.|
|`gpio`|A GPIO definition, see below|
|`blob`|Binary data (up to 64K if space in NVS), see below|
|`json`|A string `char*` internally, that is JSON in the settings|
|`s`|String i.e. `char*`|
|`c`*N*|String allowing up to *N* characters, null terminated in a `char [N+1]` array|
|`o`*N*|Fixed octet array `unit8_t [N]`, typically used with `.hex=1` or `.base32=1` or `.base64=1`, data has to be full size|
|`u8`|`uint8_t`|
|`u16`|`uint16_t`|
|`u32`|`uint32_t`|
|`u64`|`uint64_t`|
|`s8`|`int8_t`|
|`s16`|`int16_t`|
|`s32`|`int32_t`|
|`s64`|`int64_t`|

### GPIO

The `gpio` type makes a structure which has the following fields.

|Char|Field|Meaning|
|----|-----|-------|
||`.set`|If the GPIO is defined. This should be tested before using the GPIO.|
||`.num`|The GPIO number, to use in `gpio_`... functions.|
|`-`|`.invert`|If the GPIO is to be treated as logically inverted.|
|`~`|`.nopull`|If the GPIO not pulled. Default is pull up.|
|`↓`|`.pulldown`|If the GPIO is be pulled down (input) or open drain (output)|
|`↕`|`.weak`|The output is set to strength 0 (i.e. weak)|
|`⇕`|`.strong`|The output is set to strength 3, both for strength 1, neither for the default strength 2|

In many cases, in JSON, the GPIO is just a number, but if it would not be valid in JSON, e.g. `4↑` or `-0`, then it is quoted as a string value.

### Binary

The `blob` format is a structure with `.len` and `.data`.

The `o` type allows a fixed block of binary data, usually with `.hex` or `.base32` or `.base64`.

Note that numeric types allow `.hex` as well, but not with `.decimal`.

## Attributes

Additional attributes relate to each setting as follows:-

|Attribute|Meaning|
|---------|-------|
|`.array`|A number defining how many entries this has, it creates an array in JSON.|
|`.live`|This setting can be updated `live` without a reboot. If the setting is changed then it is changed in memory (as well as being stored to NVS).|
|`.hide`|This hides the setting from Advanced tab.|
|`.fix=1`|The setting is to be fixed, i.e. the default value is only used if not defined in NVS, and the value, even if default, is stored to NVS. This assumed for `gpio` type so could be set `.fix=0` for `gpio` if needed.|
|`.set`|The top bit of the value is set if the value is defined.|
|`.flags`|This is a string that are characters which can be prefixed (and/or suffixed) on a numeric value and set in the top bits of the value (see below).|
|`.hex`|The value should be hex encoded in JSON. Typically used with `o`, `blob` or even numeric values.|
|`.base32`|The value should be base32 encoded in JSON. Typically used with `o`, or `blob`|
|`.base64`|The value should be base64 encoded in JSON. Typically used with `o`, or `blob`|
|`.decimal`|Used with numeric types this scales by specified number of digits. E.g. `.decimal=2` will show `123` as `1.23` in JSON. A `#define` is for the variable suffixed with `_scale` defining the scale, e.g. `100` for `.decimal=2`.|
|`.digits`|Fixed number of zero padded digits (before decimal points)|
|`.secret`|Set if this is a secret and not output in settings JSON (or output as a dummy secret)|
|`.old`|Old name to be replaced|
|`.place`|Placeholder for settings editing|
|`.rtc=1`|Place in `RTC_NOINIT_ATTR`|

The `.set` and `.flags` attributes can apply to a numeric value, and cause top bits in the integer value to be set. `.set` is always the top bit if present, so if you have a `u16` with `.set=1` and a value of `123` is set, it will be `32891`. The `.flags` defines a string of one or more utf-8 characters that represent bits starting from the top bit (or next bit if `.set` is used as well). When parsing, any of the flags characters can be before or after the number. When output, they are normally before rhe number, unless there is a space in the flags and those characters after the space come after the number (the space is not assigned a bit). Don't use `,` in flags, and use `-` with caution.

## JSON

- A simple variable, string or number, etc, is set using a JSON string or number.
- A simple variable can be set to `null` to set default.
- A bit is set with `0`/`1`, or `true`/`false`
- An array should be set using an array, any missing final elements are cleared to zero value
- A sub object can be set, or each value can be set directly. If an object, any missing sub values are set to default
- And array or object can be set to `null` to set to defaults

## Bit fields

- Bit arrays are not supported.
- Setting a bit using JSON can set *null* as above for default value
- Setting a bit value to something starting `1`, `t` (for `true`) or `o` (for `on`) is setting it, else unsetting it.
- Duplicate fields when setting using JSON is not recommended, but is support for bit types, where the first instance is applied and later ones ignored

The reason is to allow use of `type=checkbox` on forms, a checkbox can set `on`, and a second `type=hidden` can unset. When the checkbox is set the `on` is applied, when not the hidden is applied an unset.

## Special cases

Note that the `hostname` being *unset* or set to a blank string will internally use the hex MAC address. This should generally be invisible to the settings logic, but setting to the MAC address will be seen as *no change* and so leave it blank.

## Passwords

Depending on config, a special field called `password` may exist. If set, and not blank (default), then any further password settings must start with `password` with the correct existing password setting. This means all settings then have to use JSON format.

To change the `password`, a second instance of `password` must be passed, which is not quite valid JSON. If not password is currently set, then just sending a `password` setting will set it.
