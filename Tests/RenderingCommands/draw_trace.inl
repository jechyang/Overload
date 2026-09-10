void APIENTRY PacketElements(GLenum mode,GLsizei count,GLenum type,const void* indices)
{
    Record(0,mode,count,1,type);
    // Compare data visible to each real draw, without comparing GPU object IDs.
    GLint buffer=0,program=0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,0,&buffer);
    std::array<std::byte,64> model{};glGetNamedBufferSubData(buffer,0,model.size(),model.data());
    drawTrace->write(reinterpret_cast<const char*>(model.data()),model.size());
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    std::array<float,4> tint{};glGetUniformfv(program,glGetUniformLocation(program,"tint"),tint.data());
    for(auto value:tint)Write(*drawTrace,value);
    realElements(mode,count,type,indices);
}
struct PacketDrawHooks
{
    PacketDrawHooks(std::ostream& trace){drawTrace=&trace;drawCounts={};realElements=glad_glDrawElements;glad_glDrawElements=PacketElements;}
    ~PacketDrawHooks(){glad_glDrawElements=realElements;drawTrace=nullptr;}
};
