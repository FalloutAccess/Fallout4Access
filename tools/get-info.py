# @category Test
from ghidra.app.plugin.prototype.MicrosoftCodeAnalyzerPlugin import RttiAnalyzer
from ghidra.app.util.importer import MessageLog

# Clear the RTTI Found flag from the CORRECT location
progInfo = currentProgram.getOptions("Program Information")
if progInfo.contains("RTTI Found"):
    progInfo.removeOption("RTTI Found")
    println("Cleared 'RTTI Found' from Program Information")
else:
    println("No RTTI Found flag to clear")

# Now run the analyzer
println("Running RTTI Analyzer...")
analyzer = RttiAnalyzer()
addrSet = currentProgram.getAddressFactory().getAddressSet()
log = MessageLog()
result = analyzer.added(currentProgram, addrSet, monitor, log)
println("added() returned: " + str(result))

logMsg = log.toString()
if logMsg:
    println("Log: " + logMsg)

# Check result
if progInfo.contains("RTTI Found"):
    val = progInfo.getBoolean("RTTI Found", False)
    println("RTTI Found: " + str(val))
else:
    println("RTTI Found NOT set")

println("=== Done ===")
