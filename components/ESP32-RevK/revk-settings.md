# Settings

Settings are simple named values that are stored in non volatile storage and cane be read or changed. Some settings have default values.

([details for developers](revk-settings-dev.md))

## Changing settings

Settings are normally represented as a JSON value (i.e. string, number, boolean), and you can change the value with the `setting` command, e.g. sending `setting/myapp` with payload `{"timeout":30}` would change the `timeout` setting to `30`. You can include any number of settings at once.

There is also a short cut for a single setting, e.g. sending `setting/myapp/timeout` with a payload of `30` is the same, and changes one setting at a time.

*Note that `setting/...` is the default, but the MQTT topics can be changed around to be `hostname/setting/...`*

## Seeing settings

You can see settings by sending the `setting` command with no payload, e.g. `setting/myapp` might return `{"timeout":30,"a":1,"b":2,"c":3}`. If the settings would be too long they are split over more than one message, each a JSON object.

This only includes settings stored in non volatile memory. There will be other settings, and some will have default values. Send the `setting` command with `*`, e.g. `setting/myapp/*` to see all settings that are stored in non volatile storage or that have a default value. This does not include any that are not stored and have no default value. Using `**` gives even more. For `*` or `**`. the response has a `/-` on the end so it does not itself cause settings to be set.

The response always uses the device ID, not the *hostname* form of topic.

## Secrets

Secret settings, such as passwords, are not reported using the `setting` command, but you can change them.

## Default values

Some settings have a default value, so don't need to be stored in non volatile memory. They do not show on the normal `setting` command if they are only set from the default. However, you can set them, and can even set them to the same as the default value, and they are then stored and shown.

Setting to the same as the default value may seem daft, but it does matter as a software update could have a new default value, and if you have not stored the value you want then the application will use the new default value.

You can force a setting back to not being stored, and hence to any default value, by setting it to `null`.

## GPIOs

Some settings are *forced* in to non volatile memory regardless, and GPIOs are an example of this. This is because the GPIOs relate to your board, and you do not want a later board design with different GPIOs and hence new defaults in the software to change your GPIOs. This means that even setting to `null` will still save (the current default) in non volatile storage and show when you view settings.

Some numeric settings can also have extra flags, and GPIOs are a good example. The `-` sign is a prefix that means *inverted*. This is common for an input button that may be pulled up and connected to ground when pushed, so *active* reads as `0` and *inactive* as `1`. As such you might see `{"button":-5}` meaning GPIO `5` but *inverted* logic. There are other characters such as `↓` meaning *pull down* rather than the default *pull up*.

## Zero/unset values

Some settings, and GPIO are a good example, have a concept of being *set*. You might think this is simple, `0` could be *unset* and any other number is *set*, but GPIOs are a good example where GPIO `0` is valid and different to the GPIO being *unset*. To set such a value to *unset* set it to `""`.

## Setting groups

Some settings are groups, e.g. `mqtthost` and `mqttuser` will reported in an `mqtt` group, e.g. `{"mqtt":{"host":"mqtt.iot","user":"alice"}}`.

You can set these individually, e.g. `{"mqttuser":"bob"}` would just change `mqttuser`. Or you can set in a group. But if you set in a group you have to send all settings in the group, any you miss (apart from *secret* settings) will be set to their defaults (or unset). e.g. sending `{"mqtt":{"host":"test.iot"}}` would unset `mqttuser` and any other settings (other than *secret* settings).

Sending an empty group, e.g. `{"mqtt":{}}` sets all *non secret* values to defaults. Sending `null`, e.g. `{"mqtt":null}` sets all values to defaults including *secret* values.

## Setting arrays

Some settings are an array of values, e.g. `blink` is three GPIOs (*red*, *green*, *blue*).

You can set individual values with a number, e.g. `{"blink2":5}` would change the middle value and not affect the others (yes, it starts from 1, don't ask).

If you send an array, e.g. `{"blink":[5,6]}`. All that you omit on the end will be cleared (not set to defaults, but cleared).

Sending an empty array, e.g. `{"blink":[]}` clears/unsets all values. Sending `null`, e.g. `{"blink":null}` sets the values to their defaults.

## Groups and arrays

In some cases a setting group may have settings that are arrays, e.g. `{"input":{"gpio":[1,2],"timeout":[10,20]}}`. In such cases you can turn this around and specify an array of objects, e.g. `{"input":[{"gpio":1,"timeout":10},{"gpio":2,"timeout":20}]}`.

## Secret

Secret settings are typically for passwords.

- Secrets omitted in an array are not cleared, but left unchanged, set to `""` if you want to clear.
- Secrets are not normally included in JSON output, a dummy secret is shown if `settings` with `**` and the setting is not empty.
- Secrets are not shown on web settings, a dummy secret is shown unless the setting is empty.
- You can set a secret to the dummy password, but only if it is currently am empty string, otherwise it is assumed you are not setting the secret.
- The dummy password can be set per app, but is normally `✶✶✶✶✶✶✶✶` (note these are unicode 6 pointed stars).
