package org.sumi.sumi_emu.utils

import android.content.Context
import android.content.SharedPreferences
import android.opengl.GLES30
import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.atomic.AtomicBoolean

class CASManager private constructor(private val context: Context) {
    companion object {
        private const val TAG = "CASManager"
        private const val VERTEX_SHADER_PATH = "shaders/amd_cas.vert"
        private const val FRAGMENT_SHADER_PATH = "shaders/amd_cas.frag"

        // Updated vertex data for a full-screen quad with proper 3D positions
        private val QUAD_VERTICES = floatArrayOf(
            // Position (x, y, z)    TexCoord (u, v)
            -1.0f, -1.0f, 0.0f,    0.0f, 0.0f,  // Bottom-left
             1.0f, -1.0f, 0.0f,    1.0f, 0.0f,  // Bottom-right
            -1.0f,  1.0f, 0.0f,    0.0f, 1.0f,  // Top-left
             1.0f,  1.0f, 0.0f,    1.0f, 1.0f   // Top-right
        )

        @Volatile
        private var instance: CASManager? = null

        fun getInstance(context: Context): CASManager {
            return instance ?: synchronized(this) {
                instance ?: CASManager(context.applicationContext).also { instance = it }
            }
        }
    }

    private val prefs: SharedPreferences = context.getSharedPreferences("cas_settings", Context.MODE_PRIVATE)
    private val isInitialized = AtomicBoolean(false)

    // OpenGL resources
    private var shaderProgram: Int = 0
    private var vertexBuffer: FloatBuffer? = null
    private var vertexArray: Int = 0
    private var sharpnessUniform: Int = 0
    private var qualityLevelUniform: Int = 0
    private var inputTextureUniform: Int = 0
    private var viewportScaleUniform: Int = 0

    fun initialize() {
        if (isInitialized.get()) return
        isInitialized.set(true)

        try {
            // Create vertex buffer
            vertexBuffer = ByteBuffer.allocateDirect(QUAD_VERTICES.size * 4)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer()
                .apply {
                    put(QUAD_VERTICES)
                    position(0)
                }

            // Create and compile shader program
            shaderProgram = createShaderProgram()

            // Get uniform locations
            sharpnessUniform = GLES30.glGetUniformLocation(shaderProgram, "sharpness")
            qualityLevelUniform = GLES30.glGetUniformLocation(shaderProgram, "qualityLevel")
            inputTextureUniform = GLES30.glGetUniformLocation(shaderProgram, "inputTexture")
            viewportScaleUniform = GLES30.glGetUniformLocation(shaderProgram, "viewportScale")

            // Create vertex array
            val buffers = IntArray(1)
            GLES30.glGenVertexArrays(1, buffers, 0)
            vertexArray = buffers[0]

            // Set up vertex attributes
            GLES30.glBindVertexArray(vertexArray)
            vertexBuffer?.let { buffer ->
                // Position attribute
                GLES30.glVertexAttribPointer(0, 3, GLES30.GL_FLOAT, false, 20, 0)
                GLES30.glEnableVertexAttribArray(0)

                // Texture coordinate attribute
                GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 20, 12)
                GLES30.glEnableVertexAttribArray(1)
            }
            GLES30.glBindVertexArray(0)

            // Apply initial settings
            applySettings()

            Log.d(TAG, "CAS initialized successfully")
        } catch (e: Exception) {
            Log.e(TAG, "Error initializing CAS: ${e.message}")
        }
    }

    private fun createShaderProgram(): Int {
        val vertexShader = loadShader(GLES30.GL_VERTEX_SHADER, VERTEX_SHADER_PATH)
        val fragmentShader = loadShader(GLES30.GL_FRAGMENT_SHADER, FRAGMENT_SHADER_PATH)

        val program = GLES30.glCreateProgram()
        GLES30.glAttachShader(program, vertexShader)
        GLES30.glAttachShader(program, fragmentShader)
        GLES30.glLinkProgram(program)

        // Check linking status
        val linkStatus = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, linkStatus, 0)
        if (linkStatus[0] == 0) {
            val infoLog = GLES30.glGetProgramInfoLog(program)
            Log.e(TAG, "Error linking shader program: $infoLog")
            GLES30.glDeleteProgram(program)
            return 0
        }

        return program
    }

    private fun loadShader(type: Int, path: String): Int {
        val shader = GLES30.glCreateShader(type)
        val shaderCode = context.assets.open(path).bufferedReader().use { it.readText() }

        GLES30.glShaderSource(shader, shaderCode)
        GLES30.glCompileShader(shader)

        // Check compilation status
        val compiled = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, compiled, 0)
        if (compiled[0] == 0) {
            val infoLog = GLES30.glGetShaderInfoLog(shader)
            Log.e(TAG, "Error compiling shader: $infoLog")
            GLES30.glDeleteShader(shader)
            return 0
        }

        return shader
    }

    fun applySettings() {
        if (!isInitialized.get()) return

        try {
            val enabled = prefs.getBoolean("enable_amd_cas", false)
            if (!enabled) {
                disableCAS()
                return
            }

            val sharpness = prefs.getInt("cas_sharpness", 50) / 100.0f
            val quality = prefs.getString("cas_quality", "balanced") ?: "balanced"

            // Apply quality mode settings
            val qualityLevel = when (quality) {
                "ultra" -> 4.0f
                "quality" -> 3.0f
                "balanced" -> 2.0f
                "performance" -> 1.0f
                else -> 2.0f
            }

            // Apply settings to shader
            GLES30.glUseProgram(shaderProgram)
            GLES30.glUniform1f(sharpnessUniform, sharpness)
            GLES30.glUniform1f(qualityLevelUniform, qualityLevel)

            Log.d(TAG, "CAS settings applied: enabled=$enabled, sharpness=$sharpness, quality=$quality")
        } catch (e: Exception) {
            Log.e(TAG, "Error applying CAS settings: ${e.message}")
        }
    }

    fun processFrame(inputTexture: Int, outputTexture: Int, viewportWidth: Int, viewportHeight: Int) {
        if (!isInitialized.get() || !prefs.getBoolean("enable_amd_cas", false)) return

        try {
            GLES30.glUseProgram(shaderProgram)
            GLES30.glBindVertexArray(vertexArray)

            // Calculate viewport scale for aspect ratio correction
            val scaleX = viewportWidth.toFloat() / viewportHeight.toFloat()
            GLES30.glUniform2f(viewportScaleUniform, scaleX, 1.0f)

            // Bind input texture
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, inputTexture)
            GLES30.glUniform1i(inputTextureUniform, 0)

            // Draw full-screen quad
            GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)

            // Unbind
            GLES30.glBindVertexArray(0)
            GLES30.glUseProgram(0)
        } catch (e: Exception) {
            Log.e(TAG, "Error processing frame: ${e.message}")
        }
    }

    private fun disableCAS() {
        // Reset shader state
        GLES30.glUseProgram(0)
    }

    fun release() {
        if (!isInitialized.get()) return

        try {
            GLES30.glDeleteProgram(shaderProgram)
            GLES30.glDeleteVertexArrays(1, intArrayOf(vertexArray), 0)
            vertexBuffer = null
            isInitialized.set(false)
        } catch (e: Exception) {
            Log.e(TAG, "Error releasing CAS resources: ${e.message}")
        }
    }
}