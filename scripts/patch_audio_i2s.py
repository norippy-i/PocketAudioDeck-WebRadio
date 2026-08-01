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
        "            }\n"
        "        }\n"
        "        // Close a stale file handle before unmounting a removed SD card.\n"
        "        if (m_audiofile) {\n"
        "            info(*this, evt_info, \"Closing audio file \\\"{}\\\"\", m_audiofile.name());\n"
        "            m_audiofile.close();\n"
        "        }\n"
        "        destroy_decoder();\n",
        1,
    )

    if text != original:
        audio_cpp.write_text(text)
        print("[patch_audio_i2s] patched PCM visualization and SD hot-plug hooks")
    else:
        print("[patch_audio_i2s] no changes needed")
