// Included after the panel's display helpers. Reports contain values only.
namespace
{
    std::string JsonString(const std::string& value)
    {
        std::string result="\"";constexpr char hex[]="0123456789abcdef";
        for(unsigned char c:value) {
            if(c=='"'||c=='\\'){result+='\\';result+=c;}
            else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
            else result+=c;
        }
        return result+'"';
    }
    const char* PoolCategory(const OvRendering::Core::RenderTargetPool::MemoryEntry& e)
    {
        return e.leased?(e.requiredByLatest?"activeLatest":"activeOlder"):e.pins?"pinnedOnly":e.requiredByLatest?"idleReserve":"idleUnused";
    }
}
std::string OvEditor::Panels::FrameGraph::BuildReport() const
{
    RequireUIThread();std::ostringstream out;
    out<<"{\n\"schema\":\"overload.framegraph\",\"schemaVersion\":1,\"units\":{\"time\":\"ns\",\"memory\":\"estimatedBytes\"},"
        "\"poolIsLive\":true,\"snapshot\":";
    if(!m_snapshot)out<<"null";
    else {
        const auto& g=*m_snapshot;const auto& cpu=g.cpu;
        out<<"{\"scope\":"<<g.scope<<",\"captureIndex\":"<<g.captureIndex
            <<",\"logicalTransientBytes\":"<<g.logicalTransientBytes<<",\"estimatedPeakBytes\":"<<g.estimatedPeakBytes
            <<",\"physicalTransientBytes\":"<<g.physicalTransientBytes<<",\"physicalTransientTargets\":"<<g.physicalTransientTargets
            <<",\"cpu\":{\"captured\":"<<(cpu.captured?"true":"false")<<",\"encodingDeferred\":"<<(cpu.encodingDeferred?"true":"false")
            <<",\"compile\":"<<cpu.compile<<",\"realize\":"<<cpu.realize<<",\"prepare\":"<<cpu.prepare
            <<",\"diagnostics\":"<<cpu.diagnostics<<",\"recordViews\":"<<cpu.recordViews<<",\"recordOperations\":"<<cpu.recordOperations
            <<",\"seal\":"<<cpu.seal<<",\"total\":"<<cpu.Total()<<"},\"resources\":[";
        for(size_t i=0;i<g.resources.size();++i) {
            if(i)out<<',';const auto& r=g.resources[i];const bool used=r.lifetime.first<g.passes.size();
            out<<"\n{\"index\":"<<i<<",\"generation\":"<<r.handle.generation<<",\"name\":"<<JsonString(r.name)
                <<",\"origin\":"<<JsonString(Origin(r.origin))<<",\"kind\":"<<JsonString(Kind(r.kind))
                <<",\"format\":"<<static_cast<uint32_t>(r.format)<<",\"aspect\":"<<int(r.aspect)
                <<",\"width\":"<<r.width<<",\"height\":"<<r.height<<",\"mips\":"<<r.mips<<",\"layers\":"<<r.layers<<",\"samples\":"<<r.samples
                <<",\"estimatedBytes\":"<<r.lifetime.estimatedBytes<<",\"firstPass\":"<<(used?std::to_string(r.lifetime.first):"null")
                <<",\"lastPass\":"<<(used?std::to_string(r.lifetime.last):"null")<<",\"exported\":"<<(r.lifetime.exported?"true":"false")
                <<",\"finalVersion\":"<<r.finalVersion<<",\"physicalSlot\":"<<r.physicalSlot<<'}';
        }
        out<<"],\"passes\":[";
        for(size_t p=0;p<g.passes.size();++p) {
            if(p)out<<',';const auto& pass=g.passes[p];
            out<<"\n{\"index\":"<<p<<",\"name\":"<<JsonString(pass.name)<<",\"viewId\":"<<pass.viewId<<",\"accesses\":[";
            for(size_t a=0;a<pass.accesses.size();++a) {
                if(a)out<<',';const auto& access=pass.accesses[a];
                out<<"{\"resource\":"<<access.resource.index<<",\"type\":"<<JsonString(Access(access.type))
                    <<",\"aspect\":"<<int(access.range.aspect)<<",\"firstMip\":"<<access.range.firstMip<<",\"mipCount\":"<<access.range.mipCount
                    <<",\"firstLayer\":"<<access.range.firstLayer<<",\"layerCount\":"<<access.range.layerCount
                    <<",\"load\":"<<int(access.load)<<",\"store\":"<<int(access.store)<<",\"fullCoverage\":"<<(access.fullCoverage?"true":"false")
                    <<",\"expectedVersion\":"<<(access.expectedVersion?std::to_string(*access.expectedVersion):"null");
                if(a<pass.contentVersions.size()) {
                    const auto& v=pass.contentVersions[a];
                    out<<",\"readVersion\":"<<(v.read?std::to_string(*v.read):"null")<<",\"writeVersion\":"<<(v.write?std::to_string(*v.write):"null");
                }
                out<<'}';
            }
            out<<"]}";
        }
        out<<"],\"dependencies\":[";
        for(size_t i=0;i<g.dependencies.size();++i) {
            if(i)out<<',';const auto& d=g.dependencies[i];
            out<<"{\"before\":"<<d.before<<",\"after\":"<<d.after<<",\"kind\":"<<int(d.kind)
                <<",\"resource\":"<<(d.resource<g.resources.size()?std::to_string(d.resource):"null")<<'}';
        }
        out<<"]}";
    }
    const auto& s=m_pool;
    out<<",\n\"pool\":{\"idleBytes\":"<<s.idleBytes<<",\"leasedBytes\":"<<s.leasedBytes<<",\"peakLeasedBytes\":"<<s.peakLeasedBytes
        <<",\"idleTargets\":"<<s.idleTargets<<",\"leasedTargets\":"<<s.leasedTargets<<",\"idleBudget\":"<<s.idleBudget
        <<",\"frameReserve\":"<<s.frameReserve<<",\"unusedFrameLimit\":"<<s.unusedFrameLimit
        <<",\"allocations\":"<<s.allocations<<",\"hits\":"<<s.hits<<",\"aliases\":"<<s.aliases<<",\"evictions\":"<<s.evictions<<",\"autoEvictions\":"<<s.autoEvictions
        <<",\"categories\":[";
    const char* names[]={"activeLatest","activeOlder","pinnedOnly","idleReserve","idleUnused"};
    const uint64_t bytes[]={s.activeCurrentBytes,s.activeOldBytes,s.pinnedOnlyBytes,s.idleReserveBytes,s.idleUnusedBytes};
    const size_t counts[]={s.activeCurrentTargets,s.activeOldTargets,s.pinnedOnlyTargets,s.idleReserveTargets,s.idleUnusedTargets};
    for(size_t i=0;i<5;++i){if(i)out<<',';out<<"{\"category\":"<<JsonString(names[i])<<",\"bytes\":"<<bytes[i]<<",\"targets\":"<<counts[i]<<'}';}
    out<<"],\"entries\":[";
    for(size_t i=0;i<m_poolEntries.size();++i) {
        if(i)out<<',';const auto& e=m_poolEntries[i];
        out<<"{\"slot\":"<<e.slot<<",\"category\":"<<JsonString(PoolCategory(e))<<",\"estimatedBytes\":"<<e.bytes
            <<",\"width\":"<<e.width<<",\"height\":"<<e.height<<",\"mips\":"<<e.mips<<",\"layers\":"<<e.layers<<",\"samples\":"<<e.samples
            <<",\"kind\":"<<JsonString(Kind(e.kind))<<",\"format\":"<<static_cast<uint32_t>(e.format)<<",\"unusedGraphs\":"<<e.unusedGraphs<<",\"pins\":"<<e.pins<<'}';
    }
    out<<"]},\n\"gpu\":";
    if(const auto* timings=MatchingGpuTimings()) {
        out<<"{\"scope\":"<<timings->graph->scope<<",\"captureIndex\":"<<timings->graph->captureIndex<<",\"frameID\":"<<timings->frameID
            <<",\"totalNanoseconds\":"<<timings->totalNanoseconds<<",\"passNanoseconds\":[";
        for(size_t i=0;i<timings->passNanoseconds.size();++i){if(i)out<<',';out<<timings->passNanoseconds[i];}out<<"]}";
    } else out<<"null";
    out<<"\n}\n";return out.str();
}
bool OvEditor::Panels::FrameGraph::SaveReport(const std::filesystem::path& path,std::string& error) const
{
    RequireUIThread();error.clear();
    try {
        const auto report=BuildReport();std::ofstream file;file.exceptions(std::ios::badbit|std::ios::failbit);
        file.open(path,std::ios::binary|std::ios::trunc);file.write(report.data(),static_cast<std::streamsize>(report.size()));file.close();return true;
    }catch(const std::exception& e){error=std::string("Could not save diagnostics: ")+e.what();return false;}
}
