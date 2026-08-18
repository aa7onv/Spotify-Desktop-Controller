const char mainPage[] PROGMEM = R"=====(
<HTML>
    <HEAD>
        <TITLE>Authorize Spotify</TITLE>
    </HEAD>
    <BODY>
        <CENTER>
            <B>Login.... </B>
            <a href="https://accounts.spotify.com/authorize?response_type=code&client_id=%s&redirect_uri=%s&scope=user-modify-playback-state user-read-currently-playing user-read-playback-state user-library-modify user-library-read&state=esp32cyd">Log in to spotify</a>
        </CENTER>
    </BODY>
</HTML>
)=====";

const char errorPage[] PROGMEM = R"=====(
<HTML>
    <HEAD>
        <TITLE>Authorize Spotify</TITLE>
    </HEAD>
    <BODY>
        <CENTER>
            <B>Login.... </B>
            <a href="https://accounts.spotify.com/authorize?response_type=code&client_id=%s&redirect_uri=%s&scope=user-modify-playback-state user-read-currently-playing user-read-playback-state user-library-modify user-library-read&state=esp32cyd">Log in to spotify</a>
        </CENTER>
    </BODY>
</HTML>
)=====";
