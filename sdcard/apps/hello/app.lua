-- Optional manifest. The launcher reads it to label the app; without it the
-- folder name is used. It is plain Lua rather than JSON because the device
-- already has an interpreter, so this costs no extra parser.
return {
    name = "Hello",
    version = "1.0",
    author = "alpdeck",
}
