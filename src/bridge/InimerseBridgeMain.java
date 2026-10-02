import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * Driver for the generated JNI facade.
 *
 * Prints exactly the same one-line report the Python extension prints, so the
 * two languages can be compared byte for byte:
 *
 *   &lt;version&gt; &lt;sha256-of-file&gt; &lt;top-level-statement-count&gt;
 *
 * The native library has to be reachable through {@code java.library.path};
 * the generated {@code InimerseBridge} class loads it as
 * {@code libinimerse_bridge.so} and this class never touches JNI directly.
 */
public final class InimerseBridgeMain {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            System.err.println("usage: InimerseBridgeMain <file-to-hash> <source-to-parse>");
            System.exit(2);
        }
        String source = new String(Files.readAllBytes(Paths.get(args[1])), StandardCharsets.UTF_8);
        System.out.println(InimerseBridge.version()
                + " " + InimerseBridge.sha256_file(args[0])
                + " " + InimerseBridge.parse_count(source));
    }
}
