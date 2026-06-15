
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.DataOutputStream;
import java.io.OutputStream;
import java.io.IOException;

import java.nio.file.Files;
import java.nio.file.Paths;

import java.security.MessageDigest;
import java.security.GeneralSecurityException;

import java.util.Arrays;

public class FwSign
{
    public static final byte[] MAGIC = {0x0d, (byte)0xce, 0x71, (byte)0xc9 };
    public static final String SIG_ALG = "SHA256";
    public static final long BIN_FILE_OFFSET = 0x10000; // where simple .bin code blocks get written in ESP flash

    public static final void main(String[] args) throws Exception
    {
        if (args.length < 2)
        {
            System.err.println("Need 2 args - in and out.");
            return;
        }
        byte[] inputBytes = Files.readAllBytes(Paths.get(args[0]));
        try(OutputStream fos = new FileOutputStream(args[1]))
        {
            byte[] header;
            try(ByteArrayOutputStream bos = new ByteArrayOutputStream())
            {
                try(DataOutputStream dos = new DataOutputStream(bos))
                {
                    dos.write(MAGIC, 0, MAGIC.length);
                    dos.writeShort(0); // version/flags - zero for now
                }
                header = bos.toByteArray();
            }

            // for now, we assume just one chunk - the code chunk.
            final int num_chunks = 1;
            byte[] codeChunk;
            try(ByteArrayOutputStream bos = new ByteArrayOutputStream())
            {
                writeChunk(bos, BIN_FILE_OFFSET, inputBytes);
                codeChunk = bos.toByteArray();
            }

            MessageDigest md = MessageDigest.getInstance(SIG_ALG);
            md.update(header);
            md.update(new byte[] { (byte)num_chunks });
            md.update(codeChunk);
            byte[] signature = md.digest();

            try(DataOutputStream dos = new DataOutputStream(fos))
            {
                dos.write(header, 0, header.length);
                dos.writeShort(signature.length);
                dos.write(signature, 0, signature.length);
                dos.writeByte(num_chunks);
                dos.write(codeChunk, 0, codeChunk.length);
            }
        }
    }

    private static void writeChunk(OutputStream os, long address, byte[] data) throws IOException
    {
        try(DataOutputStream dos = new DataOutputStream(os))
        {
            dos.writeInt((int)address);
            dos.writeInt(data.length);
            dos.write(data, 0, data.length);
        }
    }
}
