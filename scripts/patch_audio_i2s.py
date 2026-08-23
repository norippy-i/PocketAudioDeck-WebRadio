from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
audio_cpp = (
    project_dir
    / ".pio"
    / "libdeps"
    / env.subst("$PIOENV")
    / "ESP32-audioI2S"
    / "src"
    / "Audio.cpp"
)

if not audio_cpp.exists():
    print(f"[patch_audio_i2s] waiting for library install: {audio_cpp}")
else:
    text = audio_cpp.read_text()
    original = text

    if "pocket_audio_process_raw_samples" not in text:
        text = text.replace(
            "__attribute__((weak)) void audio_process_raw_samples(int32_t* outBuff, int16_t validSamples) {\n"
            "    // Default: do nothing. User can provide their own implementation to process audio data.\n"
            "}\n",
            "__attribute__((weak)) void audio_process_raw_samples(int32_t* outBuff, int16_t validSamples) {\n"
            "    // Default: do nothing. User can provide their own implementation to process audio data.\n"
            "}\n"
            "\n"
            "extern void pocket_audio_process_i2s(int32_t* outBuff, int16_t validSamples, bool* continueI2S);\n"
            "extern void pocket_audio_process_raw_samples(int32_t* outBuff, int16_t validSamples);\n",
            1,
        )

    text = text.replace(
        "    audio_process_raw_samples(m_outBuff.get(), m_validSamples);\n",
        "    pocket_audio_process_raw_samples(m_outBuff.get(), m_validSamples);\n",
        1,
    )
    text = text.replace(
        "        audio_process_i2s(m_resamplesBuff.get(), m_validSamples, &continueI2S);                                  // resampled stereo 32bps\n",
        "        pocket_audio_process_i2s(m_resamplesBuff.get(), m_validSamples, &continueI2S);                           // resampled stereo 32bps\n",
        1,
    )
    text = text.replace(
        "        audio_process_i2s(m_outBuff.get(), (int32_t)m_validSamples, &continueI2S);\n",
        "        pocket_audio_process_i2s(m_outBuff.get(), (int32_t)m_validSamples, &continueI2S);\n",
        1,
    )
    text = text.replace(
        "            if (m_audiofile) {\n"
        "                info(*this, evt_info, \"Closing audio file \\\"{}\\\"\", m_audiofile.name());\n"
        "                m_audiofile.close();\n"
        "            }\n"
        "        }\n"
        "        destroy_decoder();\n",
        "        }\n"
        "        // Close a stale file handle before unmounting a removed SD card.\n"
        "        if (m_audiofile) {\n"
        "            info(*this, evt_info, \"Closing audio file \\\"{}\\\"\", m_audiofile.name());\n"
        "            m_audiofile.close();\n"
        "        }\n"
        "        destroy_decoder();\n",
        1,
    )

    utf16_bom_fix = (
        '        // Some ID3 writers incorrectly mark UTF-16 text as ISO-8859-1.\n'
        '        // Prefer an actual UTF-16 BOM so Japanese title/artist text is decoded.\n'
        '        if (textEncodingByte != 1 && textEncodingByte != 2 && textDataLength >= 2 &&\n'
        '            (((uint8_t)m_ID3Hdr.iBuff[0] == 0xFF && (uint8_t)m_ID3Hdr.iBuff[1] == 0xFE) ||\n'
        '             ((uint8_t)m_ID3Hdr.iBuff[0] == 0xFE && (uint8_t)m_ID3Hdr.iBuff[1] == 0xFF))) {\n'
        '            textEncodingByte = 1;\n'
        '        }\n'
        '\n'
    )
    if utf16_bom_fix not in text:
        text = text.replace(
            '        if (textEncodingByte == 1 || textEncodingByte == 2) { // is UTF-16LE or UTF-16BE\n',
            utf16_bom_fix
            + '        if (textEncodingByte == 1 || textEncodingByte == 2) { // is UTF-16LE or UTF-16BE\n',
            1,
        )

    id3v1_utf16_fix = (
        '        field.alloc(len + 2, "field");\n'
        '        memcpy(field.get(), src, len);\n'
        '        field[len] = \'\\0\';\n'
        '        field[len + 1] = \'\\0\';\n'
        '        if (len >= 2 && ((src[0] == 0xFF && src[1] == 0xFE) ||\n'
        '                         (src[0] == 0xFE && src[1] == 0xFF))) {\n'
        '            ps_ptr<char> utf8Field;\n'
        '            utf8Field.set_name("utf8Field");\n'
        '            utf8Field.copy_from_utf16((const uint8_t*)field.get());\n'
        '            field.copy_from(utf8Field.c_get());\n'
        '        } else {\n'
        '            latinToUTF8(field);\n'
        '        }\n'
    )
    if id3v1_utf16_fix not in text:
        text = text.replace(
            '        field.alloc(len + 1, "field");\n'
            '        memcpy(field.get(), src, len);\n'
            '        field[len] = \'\\0\';\n'
            '        latinToUTF8(field);\n',
            id3v1_utf16_fix,
            1,
        )

    if text != original:
        audio_cpp.write_text(text)
        print("[patch_audio_i2s] patched PCM, SD hot-plug, and UTF-16 ID3 hooks")
    else:
        print("[patch_audio_i2s] no changes needed")
