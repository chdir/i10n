package net.sf.xfd;

import android.os.Process;

import net.sf.xfd.i10n.BuildConfig;

import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.spi.AbstractInterruptibleChannel;
import java.nio.channels.spi.AbstractSelectableChannel;
import java.nio.channels.spi.AbstractSelector;
import java.nio.channels.spi.SelectorProvider;
import java.util.Collections;
import java.util.Set;

/**
 * A simple facility for interrupting blocking native operations.
 *
 * This class works similarly to Java builtin {@link Thread} interruption and fully integrates with
 * it: interrupting a Thread by calling {@link Thread#interrupt} will set the flag inside the instance
 * of this class, if the Thread is currently running within it's {@link #begin}/{@link #end} block.
 *
 * <p>
 *
 * The actual interruption is implemented via sending a Linux signal to the thread. This may cause
 * {@code sleep} and {@code poll} to prematurely wake up, {@code read} and {@code write} to perform
 * short reads/writes, and cause whatever blocking system call is currently in progress to return
 * with {@code EINTR}. In order to <b>actually</b> stop blocking operation, the thread must check
 * the flag before retrying read/write/sleep. Java code can do so by calling {@link #isInterrupted}.
 * Native code can use the pointer returned from {@link #toNative} (it points to single {@code uint8_t},
 * set to zero, when interruption flag is cleared and non-zero otherwise).
 *
 * <p>
 *
 * Use caution, if you build custom {@link AbstractInterruptibleChannel} or {@link AbstractSelector}
 * upon this class: it uses an internal subclass of {@link AbstractSelector} to implement the
 * integration with {@link Thread#interrupt} and documentation of AbstractSelector does not describe
 * behaviour of nested calls. Most real-world implementations maintain single nullable "blocker" variable
 * without any sanity checks, but you probably should not rely on that.
 */
@NativePeer
public final class Interruption {
    static {
        System.loadLibrary("i10n-" + BuildConfig.NATIVE_VER);

        i10nInit();
    }

    private static final Cache cache = new Cache();

    private final Hack sel = new Hack(this);
    private final Thread origin = Thread.currentThread();
    private final int tid = Process.myTid();

    private final ByteBuffer buffer;
    private final long nativePtr;

    @UsedByJni
    private Interruption(long nativePtr, ByteBuffer buffer) {
        this.nativePtr = nativePtr;
        this.buffer = buffer;
    }

    /**
     * Mark the beginning of interruptible section.
     */
    public static Interruption begin() {
        final Interruption interruption = cache.get();

        interruption.doBegin();

        return interruption;
    }

    /**
     * Mark the end of interruptible section. Use with {@link #begin} to surround code blocks,
     * that need interruption support.
     */
    public static void end() {
        final Interruption interruption = cache.get();

        interruption.doEnd();
    }

    /**
     * @return whether the interruption flag of this instance is set
     */
    public boolean isInterrupted() {
        return buffer.get(0) != 0;
    }

    /**
     * @return pointer to single {@code uint8_t} for use from native code (0 == cleared)
     */
    public long toNative() {
        return nativePtr;
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            destroy(nativePtr);
        } finally {
            super.finalize();
        }
    }

    void interrupt() {
        interrupt(nativePtr, tid);
    }

    void raise() {
        buffer.put(0, (byte) 1);
    }

    void clear() {
        buffer.put(0, (byte) 0);
    }

    void doBegin() {
        sel.doBegin();

        if (origin.isInterrupted()) {
            raise();
        }
    }

    void doEnd() {
        sel.doEnd();

        clear();
    }

    /*
     * Forcibly keeps Interruption instances thread-local to prevent undefined behavior,
     * that may happen if they are garbage-collected before the signal arrives. If the thread
     * (with all it's thread locals) is garbage-collected, we can be sure, that native thread
     * is dead. And dead threads don't dispatch signals.
     */
    private static final class Cache extends ThreadLocal<Interruption> {
        @Override
        protected Interruption initialValue() {
            final Interruption i10n = newInstance();
            i10n.clear();
            return i10n;
        }
    }

    /*
     * See https://stackoverflow.com/a/29330956/1643723
     */
    private static final class Hack extends AbstractSelector {
        private static final SelectorProvider def = SelectorProvider.provider();

        private final Interruption interruption;

        protected Hack(Interruption interruption) {
            super(def);

            this.interruption = interruption;
        }

        @Override
        public Selector wakeup() {
            interruption.interrupt();
            return this;
        }

        void doBegin() {
            super.begin();
        }

        void doEnd() {
            super.end();
        }

        public Set<SelectionKey> keys() { return Collections.emptySet(); }
        public Set<SelectionKey> selectedKeys() { return Collections.emptySet(); }
        public int selectNow() { return 0; }
        public int select(long timeout) { return 0; }
        public int select() { return 0; }
        protected void implCloseSelector() {}
        protected SelectionKey register(AbstractSelectableChannel ch, int ops, Object att) {
            throw new UnsupportedOperationException();
        }
    }

    private static native void i10nInit();
    private static native Interruption newInstance();
    private static native void interrupt(long ptr, int tid);
    private static native void destroy(long ptr);
}
