static unsigned int debugger_frame = 0; /* apitrace also starts at "Frame 0" */

static void debugger_label(GLenum identifier, GLuint name, const char* fmt, ...) {

    static bool initialized = false;
    static void(*imported_glObjectLabelKHR)(GLenum identifier, GLuint name, GLsizei length, const char *label) = NULL;
    if (!initialized) {
        const GLubyte *extensions = glGetString(GL_EXTENSIONS);
        if (glo_check_extension((const GLubyte *)"GL_KHR_debug",extensions)) {
            imported_glObjectLabelKHR = glo_get_extension_proc((const GLubyte *)"glObjectLabel");
        }
        initialized = true;
    }
    if (!imported_glObjectLabelKHR) {
        return;
    }

    char buffer[512];
    va_list args;
    va_start (args, fmt);
    vsprintf (buffer, fmt, args);

    assert(glGetError()==GL_NO_ERROR);
    imported_glObjectLabelKHR(identifier, name, -1, buffer);
    while(glGetError()!=GL_NO_ERROR); //FIXME: This is necessary because GLX does always return a proc currently..

    va_end (args);
}

static void debugger_message(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start (args, fmt);
    vsprintf (buffer, fmt, args);
    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION,
                         GL_DEBUG_TYPE_MARKER, 
                         0, GL_DEBUG_SEVERITY_NOTIFICATION, -1, 
                         buffer);
    va_end (args);
}

static void debugger_push_group(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start (args, fmt);
    vsprintf (buffer, fmt, args);
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, buffer);
    va_end (args);
}

static void debugger_pop_group(void) {
    glPopDebugGroup();
}

static void debugger_finish_frame(void) {
    static bool initialized = false;
    static void(*imported_glFrameTerminatorGREMEDY)(void) = NULL;
    if (!initialized) {
        const GLubyte *extensions = glGetString(GL_EXTENSIONS);
        if (glo_check_extension((const GLubyte *)"GL_GREMEDY_frame_terminator",extensions)) {
            imported_glFrameTerminatorGREMEDY = glo_get_extension_proc((const GLubyte *)"glFrameTerminatorGREMEDY");
        }
        initialized = true;
    }
    if (imported_glFrameTerminatorGREMEDY) {
        imported_glFrameTerminatorGREMEDY();
    }
    debugger_frame++;
}

#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK

static void debugger_prepare_feedback(GLuint program, unsigned int vertex_count) {
    unsigned int varying_count = sizeof(feedback_varyings)/sizeof(feedback_varyings[0]);
    glTransformFeedbackVaryingsEXT(program, varying_count, feedback_varyings, GL_INTERLEAVED_ATTRIBS_EXT);

    static GLuint tbo = -1;
    if (tbo == -1){
        glGenBuffers(1, &tbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, tbo);
    glBufferData(GL_ARRAY_BUFFER, 4*sizeof(GLfloat)*varying_count*vertex_count, NULL, GL_STATIC_READ); /* +2 so we also get the end of the current triangle */
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, tbo);
    assert(glGetError() == 0);
}

static const gl_primitive_class_map[] = {
    [0] = -1,
    [GL_POINTS] = GL_POINTS,
    [GL_LINES] = GL_LINES,
    [GL_LINE_LOOP] = GL_LINES,
    [GL_LINE_STRIP] = GL_LINES,
    [GL_TRIANGLES] = GL_TRIANGLES,
    [GL_TRIANGLE_STRIP] = GL_TRIANGLES,
    [GL_TRIANGLE_FAN] = GL_TRIANGLES,
    [GL_QUADS] = -1,
    [GL_QUAD_STRIP] = -1,
    [GL_POLYGON] = -1
};

static bool debugger_begin_feedback(PGRAPHState* pg) {
    assert(glGetError() == 0);
    if (gl_primitive_class_map[pg->gl_primitive_mode] != -1) {
        glBeginTransformFeedbackEXT(gl_primitive_class_map[pg->gl_primitive_mode]);
        assert(glGetError() == 0);
        return true;
    }
    return false;
}

static bool debugger_end_feedback(PGRAPHState* pg) {
    assert(glGetError() == 0);
    if (gl_primitive_class_map[pg->gl_primitive_mode] != -1) {
        assert(glGetError() == 0);
        glEndTransformFeedbackEXT();
        return true;
    }
    return false;
}

static void debugger_dump_feedback(unsigned int vertex_start, unsigned int vertex_count) {
    int i;
    debugger_push_group("NV2A: Vertex Shader feedback");
    glFlush();
    assert(glGetError() == 0);
    GLint program;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    for(i = 0; i < vertex_count; i++) {
        int j;
        unsigned int varying_count = sizeof(feedback_varyings)/sizeof(feedback_varyings[0]);
        for(j = 0; j < varying_count; j++) {
            GLfloat v[4];
            glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, ((vertex_start+i)*varying_count+j)*sizeof(v), sizeof(v), v);
            debugger_message("NV2A: Feedback [%d:%d]: %16f %16f %16f %16f (%s)", program, vertex_start+i, v[0], v[1], v[2], v[3], feedback_varyings[j]);
                      printf("NV2A: Feedback [%d:%d]: %16f %16f %16f %16f (%s)\n", program, vertex_start+i, v[0], v[1], v[2], v[3], feedback_varyings[j]);
        }
    }

    assert(glGetError() == 0);
    debugger_pop_group();
}

#endif

static void debugger_export_vertex_shader(const char* file, PGRAPHState* pg, bool standalone) {
    int i;
    GLint prog;
    glGetIntegerv(GL_CURRENT_PROGRAM,&prog);
    FILE* f = fopen(file,"wb");
    if (f == NULL) {
        printf("Couldn't dump shader '%s'\n",file);
        return;
    }
    if (standalone) {
        fprintf(f,"#version 110\n"
                  "\n"
                  // Writeable registers
                  "#define attribute\n"
                  "#define uniform\n"
                  "\n"
                  // Move entrypoint
                  "void setup(void);\n"
                  "void shader(void);\n"
                  "void main(void) {\n"
                  "  setup();\n"
                  "  shader();\n"
                  "}\n"
                  "#define main(void) shader(void)\n"
                  "\n"
                  "\n// ");
    }
    char program[20*1024];
    GLsizei l;
    GLuint shader;
    glGetAttachedShaders(prog,  1,  &l,  &shader);
    glGetShaderSource(shader,  sizeof(program), &l,  program);
    program[l] = '\0';
    fprintf(f,"%s",program);
    if (standalone) {
        fprintf(f,"\n"
                  "void setup(void) {\n");
        for (i = 0; i < 192; i++) {
            float* c = pg->constants[i].data;
            if (!((c[0] == c[1]) && (c[1] == c[2]) && (c[2] == c[3]) && (fabsf(c[3]) <= 1.0e-20f))) {
                fprintf(f,"  c[%d] = vec4(%f, %f, %f, %f);\n",i,c[0],c[1],c[2],c[3]);
            }
        }
        fprintf(f,"  v0 = gl_Vertex;\n"
                  "}\n");
    }
    fclose(f);
}

static void debugger_export_mesh(const char* file, unsigned int position, unsigned int normal, unsigned int texcoord, unsigned int vcount, unsigned int icount, uint32_t* indices, unsigned int primitive) {
    int i;
    // This only works with:
    // - Type float for all attributes which must also be enabled:
    //   - 0 = Position
    //   - 3 = Normal
    //   - 6 = Texture
    // - Tristrip
    // Basicly all of this should be moved to a debugger, away from this emu
    GLint stride0,stride3,stride6;
    glGetVertexAttribiv(position, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride0);
    glGetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride3);
    glGetVertexAttribiv(6, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride6);
    uint8_t* ptr0, *ptr3, *ptr6;
    glGetVertexAttribPointerv(position, GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr0);
    glGetVertexAttribPointerv(3, GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr3);
    glGetVertexAttribPointerv(6, GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr6);
    GLint e;
    bool en = true;
    glGetVertexAttribiv(position, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &e);
    en &= e;
    glGetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &e);
    en &= e;
    glGetVertexAttribiv(6, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &e);
    en &= e;
    if (!en) {
        return;
    }
    FILE* f = fopen(file,"wb");
    if (f == NULL) {
        printf("Couldn't dump mesh '%s'\n",file);
        return;
    }
    for(i = 0; i < vcount; i++) {
        float* v0 = &ptr0[i*stride0];
        float* v3 = &ptr3[i*stride3];
        float* v6 = &ptr6[i*stride6];
        fprintf(f,"v %f %f %f 1.0\n",v0[0],v0[1],v0[2]);
        fprintf(f,"vn %f %f %f\n",v3[0],v3[1],v3[2]);
        fprintf(f,"vt %f %f\n",v6[0],v6[1]);
    }
    if (primitive == GL_TRIANGLE_STRIP) {
        int a,b;
        for (i = 0; i < icount; i++) {
            int c = indices[i]+1;
            if (i >= 2) {
                fprintf(f,"f %d/%d/%d %d/%d/%d %d/%d/%d\n", c,c,c, a,a,a, b,b,b);
            }
            a = b;
            b = c;
        }
    } else if (primitive == GL_TRIANGLES) {
        for (i = 0; i < icount/3; i++) {
            int a = indices[i*3+0]+1;
            int b = indices[i*3+1]+1;
            int c = indices[i*3+2]+1;
            fprintf(f,"f %d/%d/%d %d/%d/%d %d/%d/%d\n", c,c,c, a,a,a, b,b,b);
        }
    } else if (primitive == GL_POINTS) {
        printf("# Point meshes have no faces\n");
    } else {
        printf("# Unknown how to export 0x%x\n",primitive);
    }
    fclose(f);

}

