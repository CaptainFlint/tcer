' The script edits the PE header by replacing the OSVersion and SubsystemVersion
' fields to 5.00 (Windows 2000)
'
' Usage: pefix2000.vbs <path to file>

If WScript.Arguments.Count <> 1 Then
	Wscript.Echo "Error! No file specified."
	Wscript.Echo "Usage: pefix2000.vbs <path to file>"
	Wscript.Quit(1)
End If
FilePath = WScript.Arguments(0)
Wscript.Echo "Setting supported OS to Win2000 for the file: " & FilePath

Const adTypeBinary = 1
Const adSaveCreateOverWrite = 2

' Prepare input and output streams
Dim fi
Set fi = CreateObject("ADODB.Stream")
fi.Type = adTypeBinary
fi.Open
fi.LoadFromFile FilePath

Dim fo
Set fo = CreateObject("ADODB.Stream")
fo.Type = adTypeBinary
fo.Open

' OSVersion is located in PE header at offset 0x40
' SubsystemVersion is located in PE header at offset 0x48
' PE header's offset is written in the MZ header at offset 0x3c

' New version: 5.00 (bytes: 05 00 00 00)
version = CreateObject("System.Text.ASCIIEncoding").GetBytes_4(Chr(5) & Chr(0) & Chr(0) & Chr(0))

' Read first 60 bytes from input, write it to output
fo.Write fi.Read(&H3c)
' Get the PE offset from MZ header, copy it to output
peOffsetBin = fi.Read(4)
fo.Write peOffsetBin
' Convert 4 bytes into DWORD number: treating this as String, so bytes are joined by pairs into Unicode characters
peOffset = AscW(Mid(peOffsetBin, 1, 1)) + AscW(Mid(peOffsetBin, 2, 1)) * &H10000
' Copy bytes up until the OSVersion field
' We already copied 0x40 (0x3c + 4) bytes, and we need to copy up to start of the PE header plus 0x40 more bytes, so in total it's just the same peOffset value
fo.Write fi.Read(peOffset)
' Write new OSVersion value
fo.Write version
' Skip OSVersion bytes in the input
fi.Read(4)
' Copy 4 more bytes of input to output
fo.Write fi.Read(4)
' Write new SubsystemVersion value and skip it in the input
fo.Write version
fi.Read(4)
' Copy rest of input to output
fo.Write fi.Read

' Finished, dump the stream into the same file
fi.Close
fo.SaveToFile FilePath, adSaveCreateOverWrite
fo.Close

Wscript.Echo "Finished"
