// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-FileCopyrightText: Copyright yuzu/Citra Emulator Project / Sumi Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later


package org.sumi.sumi_emu.fragments

import android.annotation.SuppressLint
import android.app.ActivityManager
import android.app.AlertDialog
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Color
import android.net.Uri
import android.os.BatteryManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Rational
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.widget.PopupMenu
import androidx.core.content.res.ResourcesCompat
import androidx.core.graphics.Insets
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updateLayoutParams
import androidx.drawerlayout.widget.DrawerLayout
import androidx.drawerlayout.widget.DrawerLayout.DrawerListener
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.navigation.fragment.navArgs
import androidx.window.layout.FoldingFeature
import androidx.window.layout.WindowInfoTracker
import androidx.window.layout.WindowLayoutInfo
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider
import org.sumi.sumi_emu.HomeNavigationDirections
import org.sumi.sumi_emu.NativeLibrary
import org.sumi.sumi_emu.R
import org.sumi.sumi_emu.activities.EmulationActivity
import org.sumi.sumi_emu.databinding.DialogOverlayAdjustBinding
import org.sumi.sumi_emu.databinding.FragmentEmulationBinding
import org.sumi.sumi_emu.features.settings.model.BooleanSetting
import org.sumi.sumi_emu.features.settings.model.IntSetting
import org.sumi.sumi_emu.features.settings.model.Settings
import org.sumi.sumi_emu.features.settings.model.Settings.EmulationOrientation
import org.sumi.sumi_emu.features.settings.model.Settings.EmulationVerticalAlignment
import org.sumi.sumi_emu.features.settings.utils.SettingsFile
import org.sumi.sumi_emu.model.DriverViewModel
import org.sumi.sumi_emu.model.EmulationViewModel
import org.sumi.sumi_emu.model.Game
import org.sumi.sumi_emu.overlay.model.OverlayControl
import org.sumi.sumi_emu.overlay.model.OverlayLayout
import org.sumi.sumi_emu.utils.DirectoryInitialization
import org.sumi.sumi_emu.utils.FileUtil
import org.sumi.sumi_emu.utils.GameHelper
import org.sumi.sumi_emu.utils.GameIconUtils
import org.sumi.sumi_emu.utils.Log
import org.sumi.sumi_emu.utils.NativeConfig
import org.sumi.sumi_emu.utils.ViewUtils
import org.sumi.sumi_emu.utils.ViewUtils.setVisible
import org.sumi.sumi_emu.utils.collect
import java.io.File

class EmulationFragment : Fragment(), SurfaceHolder.Callback {
    private lateinit var emulationState: EmulationState
    private var emulationActivity: EmulationActivity? = null
    private var perfStatsUpdater: (() -> Unit)? = null
    private lateinit var cpuBackend: String
    private lateinit var gpuDriver: String


    private var _binding: FragmentEmulationBinding? = null
    private val binding get() = _binding!!

    private val args by navArgs<EmulationFragmentArgs>()

    private lateinit var game: Game

    private val emulationViewModel: EmulationViewModel by activityViewModels()
    private val driverViewModel: DriverViewModel by activityViewModels()

    private var isInFoldableLayout = false

    override fun onAttach(context: Context) {
        super.onAttach(context)
        if (context is EmulationActivity) {
            emulationActivity = context
            NativeLibrary.setEmulationActivity(context)
        } else {
            throw IllegalStateException("EmulationFragment must have EmulationActivity parent")
        }
    }

    /**
     * Initialize anything that doesn't depend on the layout / views in here.
     */
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        updateOrientation()

        val intentUri: Uri? = requireActivity().intent.data
        var intentGame: Game? = null
        if (intentUri != null) {
            intentGame = if (Game.extensions.contains(FileUtil.getExtension(intentUri))) {
                GameHelper.getGame(requireActivity().intent.data!!, false)
            } else {
                null
            }
        }

        try {
            game = if (args.game != null) {
                args.game!!
            } else {
                intentGame!!
            }
        } catch (e: NullPointerException) {
            Toast.makeText(
                requireContext(),
                R.string.no_game_present,
                Toast.LENGTH_SHORT
            ).show()
            requireActivity().finish()
            return
        }

        // Always load custom settings when launching a game from an intent
        if (args.custom || intentGame != null) {
            SettingsFile.loadCustomConfig(game)
            NativeConfig.unloadPerGameConfig()
        } else {
            NativeConfig.reloadGlobalConfig()
        }

        // Install the selected driver asynchronously as the game starts
        driverViewModel.onLaunchGame()

        // So this fragment doesn't restart on configuration changes; i.e. rotation.
        retainInstance = true
        emulationState = EmulationState(game.path) {
            return@EmulationState driverViewModel.isInteractionAllowed.value
        }
    }

    /**
     * Initialize the UI and start emulation in here.
     */
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentEmulationBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        if (requireActivity().isFinishing) {
            return
        }

        binding.surfaceEmulation.holder.addCallback(this)
        binding.doneControlConfig.setOnClickListener { stopConfiguringControls() }

        binding.drawerLayout.addDrawerListener(object : DrawerListener {
            override fun onDrawerSlide(drawerView: View, slideOffset: Float) {
                binding.surfaceInputOverlay.dispatchTouchEvent(
                    MotionEvent.obtain(
                        SystemClock.uptimeMillis(),
                        SystemClock.uptimeMillis() + 100,
                        MotionEvent.ACTION_UP,
                        0f,
                        0f,
                        0
                    )
                )
            }

            override fun onDrawerOpened(drawerView: View) {
                binding.drawerLayout.setDrawerLockMode(DrawerLayout.LOCK_MODE_UNLOCKED)
                binding.inGameMenu.requestFocus()
                emulationViewModel.setDrawerOpen(true)
            }

            override fun onDrawerClosed(drawerView: View) {
                binding.drawerLayout.setDrawerLockMode(IntSetting.LOCK_DRAWER.getInt())
                emulationViewModel.setDrawerOpen(false)
            }

            override fun onDrawerStateChanged(newState: Int) {
                // No op
            }
        })
        binding.drawerLayout.setDrawerLockMode(DrawerLayout.LOCK_MODE_LOCKED_CLOSED)
        binding.inGameMenu.getHeaderView(0).apply {
            val titleView = findViewById<TextView>(R.id.text_game_title)
            titleView.text = game.title
        }

        binding.inGameMenu.menu.findItem(R.id.menu_lock_drawer).apply {
            val lockMode = IntSetting.LOCK_DRAWER.getInt()
            val titleId = if (lockMode == DrawerLayout.LOCK_MODE_LOCKED_CLOSED) {
                R.string.unlock_drawer
            } else {
                R.string.lock_drawer
            }
            val iconId = if (lockMode == DrawerLayout.LOCK_MODE_UNLOCKED) {
                R.drawable.ic_unlock
            } else {
                R.drawable.ic_lock
            }

            title = getString(titleId)
            icon = ResourcesCompat.getDrawable(
                resources,
                iconId,
                requireContext().theme
            )
        }

        binding.inGameMenu.setNavigationItemSelectedListener {
            when (it.itemId) {
                R.id.menu_pause_emulation -> {
                    if (emulationState.isPaused) {
                        emulationState.run(false)
                        it.title = resources.getString(R.string.emulation_pause)
                        it.icon = ResourcesCompat.getDrawable(
                            resources,
                            R.drawable.ic_pause,
                            requireContext().theme
                        )
                    } else {
                        emulationState.pause()
                        it.title = resources.getString(R.string.emulation_unpause)
                        it.icon = ResourcesCompat.getDrawable(
                            resources,
                            R.drawable.ic_play,
                            requireContext().theme
                        )
                    }
                    binding.inGameMenu.requestFocus()
                    true
                }

                R.id.menu_settings -> {
                    val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                        null,
                        Settings.MenuTag.SECTION_ROOT
                    )
                    binding.inGameMenu.requestFocus()
                    binding.root.findNavController().navigate(action)
                    true
                }

                R.id.menu_settings_per_game -> {
                    val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                        args.game,
                        Settings.MenuTag.SECTION_ROOT
                    )
                    binding.inGameMenu.requestFocus()
                    binding.root.findNavController().navigate(action)
                    true
                }


                R.id.menu_multiplayer -> {
                    emulationActivity?.displayMultiplayerDialog()
                    true
                }


                R.id.menu_controls -> {
                    val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                        null,
                        Settings.MenuTag.SECTION_INPUT
                    )
                    binding.root.findNavController().navigate(action)
                    true
                }

                R.id.menu_overlay_controls -> {
                    showOverlayOptions()
                    true
                }

                R.id.menu_lock_drawer -> {
                    when (IntSetting.LOCK_DRAWER.getInt()) {
                        DrawerLayout.LOCK_MODE_UNLOCKED -> {
                            IntSetting.LOCK_DRAWER.setInt(DrawerLayout.LOCK_MODE_LOCKED_CLOSED)
                            it.title = resources.getString(R.string.unlock_drawer)
                            it.icon = ResourcesCompat.getDrawable(
                                resources,
                                R.drawable.ic_lock,
                                requireContext().theme
                            )
                        }

                        DrawerLayout.LOCK_MODE_LOCKED_CLOSED -> {
                            IntSetting.LOCK_DRAWER.setInt(DrawerLayout.LOCK_MODE_UNLOCKED)
                            it.title = resources.getString(R.string.lock_drawer)
                            it.icon = ResourcesCompat.getDrawable(
                                resources,
                                R.drawable.ic_unlock,
                                requireContext().theme
                            )
                        }
                    }
                    binding.inGameMenu.requestFocus()
                    NativeConfig.saveGlobalConfig()
                    true
                }

                R.id.menu_exit -> {
                    emulationState.stop()
                    NativeConfig.reloadGlobalConfig()
                    emulationViewModel.setIsEmulationStopping(true)
                    binding.drawerLayout.close()
                    binding.inGameMenu.requestFocus()
                    true
                }

                else -> true
            }
        }

        setInsets()

        requireActivity().onBackPressedDispatcher.addCallback(
            requireActivity(),
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() {
                    if (!NativeLibrary.isRunning()) {
                        return
                    }
                    emulationViewModel.setDrawerOpen(!binding.drawerLayout.isOpen)
                }
            }
        )

        GameIconUtils.loadGameIcon(game, binding.loadingImage)
        binding.loadingTitle.text = game.title
        binding.loadingTitle.isSelected = true
        binding.loadingText.isSelected = true

        WindowInfoTracker.getOrCreate(requireContext())
            .windowLayoutInfo(requireActivity()).collect(viewLifecycleOwner) {
                updateFoldableLayout(requireActivity() as EmulationActivity, it)
            }
        emulationViewModel.shaderProgress.collect(viewLifecycleOwner) {
            if (it > 0 && it != emulationViewModel.totalShaders.value) {
                binding.loadingProgressIndicator.isIndeterminate = false

                if (it < binding.loadingProgressIndicator.max) {
                    binding.loadingProgressIndicator.progress = it
                }
            }

            if (it == emulationViewModel.totalShaders.value) {
                binding.loadingText.setText(R.string.loading)
                binding.loadingProgressIndicator.isIndeterminate = true
            }
        }
        emulationViewModel.totalShaders.collect(viewLifecycleOwner) {
            binding.loadingProgressIndicator.max = it
        }
        emulationViewModel.shaderMessage.collect(viewLifecycleOwner) {
            if (it.isNotEmpty()) {
                binding.loadingText.text = it
            }
        }

        emulationViewModel.emulationStarted.collect(viewLifecycleOwner) {
            if (it) {
                binding.drawerLayout.setDrawerLockMode(IntSetting.LOCK_DRAWER.getInt())
                ViewUtils.showView(binding.surfaceInputOverlay)
                ViewUtils.hideView(binding.loadingIndicator)

                emulationState.updateSurface()

                // Setup overlays
                updateShowStatsOverlay()

                // Re update binding when the specs values get initialized properly
                binding.inGameMenu.getHeaderView(0).apply {
                    val titleView = findViewById<TextView>(R.id.text_game_title)
                    val cpuBackendLabel = findViewById<TextView>(R.id.cpu_backend)
                    val gpuvendorLabel = findViewById<TextView>(R.id.gpu_vendor)

                    titleView.text = game.title
                    cpuBackendLabel.text = NativeLibrary.getCpuBackend()
                    gpuvendorLabel.text = NativeLibrary.getGpuDriver()
                }


                val position = IntSetting.PERF_OVERLAY_POSITION.getInt()
                updateStatsPosition(position)

            }
        }
        emulationViewModel.isEmulationStopping.collect(viewLifecycleOwner) {
            if (it) {
                binding.loadingText.setText(R.string.shutting_down)
                ViewUtils.showView(binding.loadingIndicator)
                ViewUtils.hideView(binding.inputContainer)
                ViewUtils.hideView(binding.showStatsOverlayText)
            }
        }
        emulationViewModel.drawerOpen.collect(viewLifecycleOwner) {
            if (it) {
                binding.drawerLayout.open()
                binding.inGameMenu.requestFocus()
            } else {
                binding.drawerLayout.close()
            }
        }
        emulationViewModel.programChanged.collect(viewLifecycleOwner) {
            if (it != 0) {
                emulationViewModel.setEmulationStarted(false)
                binding.drawerLayout.close()
                binding.drawerLayout
                    .setDrawerLockMode(DrawerLayout.LOCK_MODE_LOCKED_CLOSED)
                ViewUtils.hideView(binding.surfaceInputOverlay)
                ViewUtils.showView(binding.loadingIndicator)
            }
        }
        emulationViewModel.emulationStopped.collect(viewLifecycleOwner) {
            if (it && emulationViewModel.programChanged.value != -1) {
                if (perfStatsUpdater != null) {
                    perfStatsUpdateHandler.removeCallbacks(perfStatsUpdater!!)
                }
                emulationState.changeProgram(emulationViewModel.programChanged.value)
                emulationViewModel.setProgramChanged(-1)
                emulationViewModel.setEmulationStopped(false)
            }
        }

        driverViewModel.isInteractionAllowed.collect(viewLifecycleOwner) {
            if (it) startEmulation()
        }
    }

    private fun startEmulation(programIndex: Int = 0) {
        if (!NativeLibrary.isRunning() && !NativeLibrary.isPaused()) {
            if (!DirectoryInitialization.areDirectoriesReady) {
                DirectoryInitialization.start()
            }

            updateScreenLayout()

            emulationState.run(emulationActivity!!.isActivityRecreated, programIndex)
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (_binding == null) {
            return
        }

        updateScreenLayout()
        val showInputOverlay = BooleanSetting.SHOW_INPUT_OVERLAY.getBoolean()
        if (emulationActivity?.isInPictureInPictureMode == true) {
            if (binding.drawerLayout.isOpen) {
                binding.drawerLayout.close()
            }
            if (showInputOverlay) {
                binding.surfaceInputOverlay.setVisible(visible = false, gone = false)
            }
        } else {
            binding.surfaceInputOverlay.setVisible(
                showInputOverlay && emulationViewModel.emulationStarted.value
            )
            if (!isInFoldableLayout) {
                if (newConfig.orientation == Configuration.ORIENTATION_PORTRAIT) {
                    binding.surfaceInputOverlay.layout = OverlayLayout.Portrait
                } else {
                    binding.surfaceInputOverlay.layout = OverlayLayout.Landscape
                }
            }
        }
    }

    override fun onPause() {
        if (emulationState.isRunning && emulationActivity?.isInPictureInPictureMode != true) {
            emulationState.pause()
        }
        super.onPause()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    override fun onDetach() {
        NativeLibrary.clearEmulationActivity()
        super.onDetach()
    }
    override fun onResume() {
        super.onResume()
        // If the overlay is enabled, we need to update the position if changed
        val position = IntSetting.PERF_OVERLAY_POSITION.getInt()
        updateStatsPosition(position)
    }

    private fun resetInputOverlay() {
        IntSetting.OVERLAY_SCALE.reset()
        IntSetting.OVERLAY_OPACITY.reset()
        binding.surfaceInputOverlay.post {
            binding.surfaceInputOverlay.resetLayoutVisibilityAndPlacement()
        }
    }
    @SuppressLint("DefaultLocale")
    private fun updateShowStatsOverlay() {
        val showOverlay = BooleanSetting.SHOW_PERFORMANCE_OVERLAY.getBoolean()
        binding.showStatsOverlayText.apply {
            setTextColor(
                MaterialColors.getColor(
                    this,
                    com.google.android.material.R.attr.colorPrimary
               )
            )
        }
        binding.showStatsOverlayText.setVisible(showOverlay)
        if (showOverlay) {
            val SYSTEM_FPS = 0
            val FPS = 1
            val FRAMETIME = 2
            val SPEED = 3
            val sb = StringBuilder()
            perfStatsUpdater = {
                if (emulationViewModel.emulationStarted.value &&
                    !emulationViewModel.isEmulationStopping.value
                ) {
                    sb.setLength(0)

                    val perfStats = NativeLibrary.getPerfStats()
                    val actualFps = perfStats[FPS]

                    if (BooleanSetting.SHOW_FPS.getBoolean(NativeConfig.isPerGameConfigLoaded())) {
                        val enableFrameInterpolation = BooleanSetting.FRAME_INTERPOLATION.getBoolean()
//                        val enableFrameSkipping = BooleanSetting.FRAME_SKIPPING.getBoolean()

                        var fpsText = String.format("FPS: %.1f", actualFps)

                        if (enableFrameInterpolation) {
                            fpsText += " " + getString(R.string.enhanced_fps_suffix)
                        }

//                        if (enableFrameSkipping) {
//                            fpsText += " " + getString(R.string.skipping_fps_suffix)
//                        }

                        sb.append(fpsText)
                    }

                    if (BooleanSetting.SHOW_FRAMETIME.getBoolean(NativeConfig.isPerGameConfigLoaded())) {
                        if (sb.isNotEmpty()) sb.append(" | ")
                        sb.append(
                            String.format(
                                "FT: %.1fms",
                                (perfStats[FRAMETIME] * 1000.0f).toFloat()
                            )
                        )
                    }

                    if (BooleanSetting.SHOW_APP_RAM_USAGE.getBoolean(NativeConfig.isPerGameConfigLoaded())) {
                        if (sb.isNotEmpty()) sb.append(" | ")
                        val appRamUsage = File("/proc/self/statm").readLines()[0].split(' ')[1].toLong() * 4096 / 1000000
                        sb.append("Process RAM: $appRamUsage MB")
                    }

                    if (BooleanSetting.SHOW_SYSTEM_RAM_USAGE.getBoolean(NativeConfig.isPerGameConfigLoaded())) {
                        if (sb.isNotEmpty()) sb.append(" | ")
                        context?.let { ctx ->
                            val activityManager = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
                            val memInfo = ActivityManager.MemoryInfo()
                            activityManager.getMemoryInfo(memInfo)
                            val usedRamMB = (memInfo.totalMem - memInfo.availMem) / 1048576L
                            sb.append("RAM: $usedRamMB MB")
                        }
                    }

                    if (BooleanSetting.SHOW_BAT_TEMPERATURE.getBoolean(NativeConfig.isPerGameConfigLoaded())) {
                        if (sb.isNotEmpty()) sb.append(" | ")
                        val batteryTemp = getBatteryTemperature()
                        val tempF = celsiusToFahrenheit(batteryTemp)
                        sb.append(String.format("%.1f°C/%.1f°F", batteryTemp, tempF))
                    }

                    val shadersBuilding = NativeLibrary.getShadersBuilding()

                    if (BooleanSetting.SHOW_SHADERS_BUILDING.getBoolean(NativeConfig.isPerGameConfigLoaded()) && shadersBuilding != 0) {
                        if (sb.isNotEmpty()) sb.append(" | ")

                        val prefix = getString(R.string.shaders_prefix)
                        val suffix = getString(R.string.shaders_suffix)
                        sb.append(String.format("$prefix %d $suffix", shadersBuilding))
                    }

                    if (BooleanSetting.OVERLAY_BACKGROUND.getBoolean(NativeConfig.isPerGameConfigLoaded())) {
                        binding.showStatsOverlayText.setBackgroundResource(R.color.sumi_transparent_black)
                    } else {
                        binding.showStatsOverlayText.setBackgroundResource(0)
                    }

                    binding.showStatsOverlayText.text = sb.toString()
                }
                perfStatsUpdateHandler.postDelayed(perfStatsUpdater!!, 800)
            }
            perfStatsUpdateHandler.post(perfStatsUpdater!!)
        } else {
            if (perfStatsUpdater != null) {
                perfStatsUpdateHandler.removeCallbacks(perfStatsUpdater!!)
            }
        }
    }

    private fun updateStatsPosition(position: Int) {
        val params = binding.showStatsOverlayText.layoutParams as FrameLayout.LayoutParams
        when (position) {
            0 -> {
                params.gravity = (Gravity.TOP or Gravity.START)
                params.setMargins(resources.getDimensionPixelSize(R.dimen.spacing_large), 0, 0, 0)
            }

            1 -> {
                params.gravity = (Gravity.TOP or Gravity.CENTER_HORIZONTAL)
            }

            2 -> {
                params.gravity = (Gravity.TOP or Gravity.END)
                params.setMargins(0, 0, resources.getDimensionPixelSize(R.dimen.spacing_large), 0)
            }

            3 -> {
                params.gravity = (Gravity.BOTTOM or Gravity.START)
                params.setMargins(resources.getDimensionPixelSize(R.dimen.spacing_large), 0, 0, 0)
            }

            4 -> {
                params.gravity = (Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL)
            }

            5 -> {
                params.gravity = (Gravity.BOTTOM or Gravity.END)
                params.setMargins(0, 0, resources.getDimensionPixelSize(R.dimen.spacing_large), 0)
            }
        }
    }

    private fun getBatteryTemperature(): Float {
        try {
            val batteryIntent = requireContext().registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            // Temperature in tenths of a degree Celsius
            val temperature = batteryIntent?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 0
            // Convert to degrees Celsius
            return temperature / 10.0f
        } catch (e: Exception) {
            return 0.0f
        }
    }
    private fun celsiusToFahrenheit(celsius: Float): Float {
        return (celsius * 9 / 5) + 32
    }

    private fun updateThermalOverlay(temperature: Float) {
        if (BooleanSetting.SHOW_THERMAL_OVERLAY.getBoolean() &&
            emulationViewModel.emulationStarted.value &&
            !emulationViewModel.isEmulationStopping.value
        ) {
            // Convert to Fahrenheit
            val fahrenheit = (temperature * 9f / 5f) + 32f

            // Determine color based on temperature ranges
            val color = when {
                temperature < 35  -> Color.parseColor("#00C8FF")
                temperature < 40  -> Color.parseColor("#A146FF")
                temperature < 45  -> Color.parseColor("#FFA500")
                else              -> Color.RED
            }

            binding.showThermalsText.setTextColor(color)
            binding.showThermalsText.text = String.format("%.1f°C • %.1f°F", temperature, fahrenheit)
        }
    }


    @SuppressLint("SourceLockedOrientationActivity")
    private fun updateOrientation() {
        emulationActivity?.let {
            val orientationSetting =
                EmulationOrientation.from(IntSetting.RENDERER_SCREEN_LAYOUT.getInt())
            it.requestedOrientation = when (orientationSetting) {
                EmulationOrientation.Unspecified -> ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                EmulationOrientation.SensorLandscape ->
                    ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE

                EmulationOrientation.Landscape -> ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                EmulationOrientation.ReverseLandscape ->
                    ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE

                EmulationOrientation.SensorPortrait ->
                    ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT

                EmulationOrientation.Portrait -> ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                EmulationOrientation.ReversePortrait ->
                    ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT
            }
        }
    }

    private fun updateScreenLayout() {
        val verticalAlignment =
            EmulationVerticalAlignment.from(IntSetting.VERTICAL_ALIGNMENT.getInt())
        val aspectRatio = when (IntSetting.RENDERER_ASPECT_RATIO.getInt()) {
            0 -> Rational(16, 9)
            1 -> Rational(4, 3)
            2 -> Rational(21, 9)
            3 -> Rational(16, 10)
            else -> null // Best fit
        }
        when (verticalAlignment) {
            EmulationVerticalAlignment.Top -> {
                binding.surfaceEmulation.setAspectRatio(aspectRatio)
                val params = FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
                )
                params.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
                binding.surfaceEmulation.layoutParams = params
            }

            EmulationVerticalAlignment.Center -> {
                binding.surfaceEmulation.setAspectRatio(null)
                binding.surfaceEmulation.updateLayoutParams {
                    width = ViewGroup.LayoutParams.MATCH_PARENT
                    height = ViewGroup.LayoutParams.MATCH_PARENT
                }
            }

            EmulationVerticalAlignment.Bottom -> {
                binding.surfaceEmulation.setAspectRatio(aspectRatio)
                val params =
                    FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                    )
                params.gravity = Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
                binding.surfaceEmulation.layoutParams = params
            }
        }
        emulationState.updateSurface()
        emulationActivity?.buildPictureInPictureParams()
        updateOrientation()
    }

    private fun updateFoldableLayout(
        emulationActivity: EmulationActivity,
        newLayoutInfo: WindowLayoutInfo
    ) {
        val isFolding =
            (newLayoutInfo.displayFeatures.find { it is FoldingFeature } as? FoldingFeature)?.let {
                if (it.isSeparating) {
                    emulationActivity.requestedOrientation =
                        ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                    if (it.orientation == FoldingFeature.Orientation.HORIZONTAL) {
                        // Restrict emulation and overlays to the top of the screen
                        binding.emulationContainer.layoutParams.height = it.bounds.top
                        // Restrict input and menu drawer to the bottom of the screen
                        binding.inputContainer.layoutParams.height = it.bounds.bottom
                        binding.inGameMenu.layoutParams.height = it.bounds.bottom

                        isInFoldableLayout = true
                        binding.surfaceInputOverlay.layout = OverlayLayout.Foldable
                    }
                }
                it.isSeparating
            } ?: false
        if (!isFolding) {
            binding.emulationContainer.layoutParams.height = ViewGroup.LayoutParams.MATCH_PARENT
            binding.inputContainer.layoutParams.height = ViewGroup.LayoutParams.MATCH_PARENT
            binding.inGameMenu.layoutParams.height = ViewGroup.LayoutParams.MATCH_PARENT
            isInFoldableLayout = false
            updateOrientation()
            onConfigurationChanged(resources.configuration)
        }
        binding.emulationContainer.requestLayout()
        binding.inputContainer.requestLayout()
        binding.inGameMenu.requestLayout()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        // We purposely don't do anything here.
        // All work is done in surfaceChanged, which we are guaranteed to get even for surface creation.
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.debug("[EmulationFragment] Surface changed. Resolution: " + width + "x" + height)
        emulationState.newSurface(holder.surface)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        emulationState.clearSurface()
    }

    private fun showOverlayOptions() {
        val anchor = binding.inGameMenu.findViewById<View>(R.id.menu_overlay_controls)
        val popup = PopupMenu(requireContext(), anchor)

        popup.menuInflater.inflate(R.menu.menu_overlay_options, popup.menu)

        popup.menu.apply {
            findItem(R.id.menu_show_stats_overlay).isChecked =
                BooleanSetting.SHOW_PERFORMANCE_OVERLAY.getBoolean()
            findItem(R.id.menu_rel_stick_center).isChecked =
                BooleanSetting.JOYSTICK_REL_CENTER.getBoolean()
            findItem(R.id.menu_dpad_slide).isChecked = BooleanSetting.DPAD_SLIDE.getBoolean()
            findItem(R.id.menu_show_overlay).isChecked =
                BooleanSetting.SHOW_INPUT_OVERLAY.getBoolean()
            findItem(R.id.menu_haptics).isChecked = BooleanSetting.HAPTIC_FEEDBACK.getBoolean()
            findItem(R.id.menu_touchscreen).isChecked = BooleanSetting.TOUCHSCREEN.getBoolean()
        }

        popup.setOnDismissListener { NativeConfig.saveGlobalConfig() }
        popup.setOnMenuItemClickListener {
            when (it.itemId) {
                R.id.menu_show_stats_overlay -> {
                    it.isChecked = !it.isChecked
                    BooleanSetting.SHOW_PERFORMANCE_OVERLAY.setBoolean(it.isChecked)
                    updateShowStatsOverlay()
                    true
                }
                R.id.menu_edit_overlay -> {
                    binding.drawerLayout.close()
                    binding.surfaceInputOverlay.requestFocus()
                    startConfiguringControls()
                    true
                }

                R.id.menu_adjust_overlay -> {
                    adjustOverlay()
                    true
                }

                R.id.menu_toggle_controls -> {
                    val overlayControlData = NativeConfig.getOverlayControlData()
                    val optionsArray = BooleanArray(overlayControlData.size)
                    overlayControlData.forEachIndexed { i, _ ->
                        optionsArray[i] = overlayControlData.firstOrNull { data ->
                            OverlayControl.entries[i].id == data.id
                        }?.enabled == true
                    }

                    val dialog = MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.emulation_toggle_controls)
                        .setMultiChoiceItems(
                            R.array.gamepadButtons,
                            optionsArray
                        ) { _, indexSelected, isChecked ->
                            overlayControlData.firstOrNull { data ->
                                OverlayControl.entries[indexSelected].id == data.id
                            }?.enabled = isChecked
                        }
                        .setPositiveButton(android.R.string.ok) { _, _ ->
                            NativeConfig.setOverlayControlData(overlayControlData)
                            NativeConfig.saveGlobalConfig()
                            binding.surfaceInputOverlay.refreshControls()
                        }
                        .setNegativeButton(android.R.string.cancel, null)
                        .setNeutralButton(R.string.emulation_toggle_all) { _, _ -> }
                        .show()

                    // Override normal behaviour so the dialog doesn't close
                    dialog.getButton(AlertDialog.BUTTON_NEUTRAL)
                        .setOnClickListener {
                            val isChecked = !optionsArray[0]
                            overlayControlData.forEachIndexed { i, _ ->
                                optionsArray[i] = isChecked
                                dialog.listView.setItemChecked(i, isChecked)
                                overlayControlData[i].enabled = isChecked
                            }
                        }
                    true
                }

                R.id.menu_show_overlay -> {
                    it.isChecked = !it.isChecked
                    BooleanSetting.SHOW_INPUT_OVERLAY.setBoolean(it.isChecked)
                    binding.surfaceInputOverlay.refreshControls()
                    true
                }

                R.id.menu_rel_stick_center -> {
                    it.isChecked = !it.isChecked
                    BooleanSetting.JOYSTICK_REL_CENTER.setBoolean(it.isChecked)
                    true
                }

                R.id.menu_dpad_slide -> {
                    it.isChecked = !it.isChecked
                    BooleanSetting.DPAD_SLIDE.setBoolean(it.isChecked)
                    true
                }

                R.id.menu_haptics -> {
                    it.isChecked = !it.isChecked
                    BooleanSetting.HAPTIC_FEEDBACK.setBoolean(it.isChecked)
                    true
                }

                R.id.menu_touchscreen -> {
                    it.isChecked = !it.isChecked
                    BooleanSetting.TOUCHSCREEN.setBoolean(it.isChecked)
                    true
                }

                R.id.menu_reset_overlay -> {
                    binding.drawerLayout.close()
                    resetInputOverlay()
                    true
                }

                else -> true
            }
        }

        popup.show()
    }

    @SuppressLint("SourceLockedOrientationActivity")
    private fun startConfiguringControls() {
        // Lock the current orientation to prevent editing inconsistencies
        if (IntSetting.RENDERER_SCREEN_LAYOUT.getInt() == EmulationOrientation.Unspecified.int) {
            emulationActivity?.let {
                it.requestedOrientation =
                    if (resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
                        ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT
                    } else {
                        ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
                    }
            }
        }
        binding.doneControlConfig.setVisible(true)
        binding.surfaceInputOverlay.setIsInEditMode(true)
    }

    private fun stopConfiguringControls() {
        binding.doneControlConfig.setVisible(false)
        binding.surfaceInputOverlay.setIsInEditMode(false)
        // Unlock the orientation if it was locked for editing
        if (IntSetting.RENDERER_SCREEN_LAYOUT.getInt() == EmulationOrientation.Unspecified.int) {
            emulationActivity?.let {
                it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            }
        }
        NativeConfig.saveGlobalConfig()
    }

    @SuppressLint("SetTextI18n")
    private fun adjustOverlay() {
        val adjustBinding = DialogOverlayAdjustBinding.inflate(layoutInflater)
        adjustBinding.apply {
            inputScaleSlider.apply {
                valueTo = 150F
                value = IntSetting.OVERLAY_SCALE.getInt().toFloat()
                addOnChangeListener(
                    Slider.OnChangeListener { _, value, _ ->
                        inputScaleValue.text = "${value.toInt()}%"
                        setControlScale(value.toInt())
                    }
                )
            }
            inputOpacitySlider.apply {
                valueTo = 100F
                value = IntSetting.OVERLAY_OPACITY.getInt().toFloat()
                addOnChangeListener(
                    Slider.OnChangeListener { _, value, _ ->
                        inputOpacityValue.text = "${value.toInt()}%"
                        setControlOpacity(value.toInt())
                    }
                )
            }
            inputScaleValue.text = "${inputScaleSlider.value.toInt()}%"
            inputOpacityValue.text = "${inputOpacitySlider.value.toInt()}%"
        }

        MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.emulation_control_adjust)
            .setView(adjustBinding.root)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface?, _: Int ->
                NativeConfig.saveGlobalConfig()
            }
            .setNeutralButton(R.string.slider_default) { _: DialogInterface?, _: Int ->
                setControlScale(50)
                setControlOpacity(100)
            }
            .show()
    }

    private fun setControlScale(scale: Int) {
        IntSetting.OVERLAY_SCALE.setInt(scale)
        binding.surfaceInputOverlay.refreshControls()
    }

    private fun setControlOpacity(opacity: Int) {
        IntSetting.OVERLAY_OPACITY.setInt(opacity)
        binding.surfaceInputOverlay.refreshControls()
    }

    private fun setInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.inGameMenu
        ) { v: View, windowInsets: WindowInsetsCompat ->
            val cutInsets: Insets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            var left = 0
            var right = 0
            if (ViewCompat.getLayoutDirection(v) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                left = cutInsets.left
            } else {
                right = cutInsets.right
            }

            v.setPadding(left, cutInsets.top, right, 0)

            windowInsets
        }
    }

    private class EmulationState(
        private val gamePath: String,
        private val emulationCanStart: () -> Boolean
    ) {
        private var state: State
        private var surface: Surface? = null
        lateinit var emulationThread: Thread

        init {
            // Starting state is stopped.
            state = State.STOPPED
        }

        @get:Synchronized
        val isStopped: Boolean
            get() = state == State.STOPPED

        // Getters for the current state
        @get:Synchronized
        val isPaused: Boolean
            get() = state == State.PAUSED

        @get:Synchronized
        val isRunning: Boolean
            get() = state == State.RUNNING

        @Synchronized
        fun stop() {
            if (state != State.STOPPED) {
                Log.debug("[EmulationFragment] Stopping emulation.")
                NativeLibrary.stopEmulation()
                state = State.STOPPED
            } else {
                Log.warning("[EmulationFragment] Stop called while already stopped.")
            }
        }

        // State changing methods
        @Synchronized
        fun pause() {
            if (state != State.PAUSED) {
                Log.debug("[EmulationFragment] Pausing emulation.")

                NativeLibrary.pauseEmulation()

                state = State.PAUSED
            } else {
                Log.warning("[EmulationFragment] Pause called while already paused.")
            }
        }

        @Synchronized
        fun run(isActivityRecreated: Boolean, programIndex: Int = 0) {
            if (isActivityRecreated) {
                if (NativeLibrary.isRunning()) {
                    state = State.PAUSED
                }
            } else {
                Log.debug("[EmulationFragment] activity resumed or fresh start")
            }

            // If the surface is set, run now. Otherwise, wait for it to get set.
            if (surface != null) {
                runWithValidSurface(programIndex)
            }
        }

        @Synchronized
        fun changeProgram(programIndex: Int) {
            emulationThread.join()
            emulationThread = Thread({
                Log.debug("[EmulationFragment] Starting emulation thread.")
                NativeLibrary.run(gamePath, programIndex, false)
            }, "NativeEmulation")
            emulationThread.start()
        }

        // Surface callbacks
        @Synchronized
        fun newSurface(surface: Surface?) {
            this.surface = surface
            if (this.surface != null) {
                runWithValidSurface()
            }
        }

        @Synchronized
        fun updateSurface() {
            if (surface != null) {
                NativeLibrary.surfaceChanged(surface)
            }
        }

        @Synchronized
        fun clearSurface() {
            if (surface == null) {
                Log.warning("[EmulationFragment] clearSurface called, but surface already null.")
            } else {
                surface = null
                Log.debug("[EmulationFragment] Surface destroyed.")
                when (state) {
                    State.RUNNING -> {
                        state = State.PAUSED
                    }

                    State.PAUSED -> Log.warning(
                        "[EmulationFragment] Surface cleared while emulation paused."
                    )

                    else -> Log.warning(
                        "[EmulationFragment] Surface cleared while emulation stopped."
                    )
                }
            }
        }

        private fun runWithValidSurface(programIndex: Int = 0) {
            NativeLibrary.surfaceChanged(surface)
            if (!emulationCanStart.invoke()) {
                return
            }

            when (state) {
                State.STOPPED -> {
                    emulationThread = Thread({
                        Log.debug("[EmulationFragment] Starting emulation thread.")
                        NativeLibrary.run(gamePath, programIndex, true)
                    }, "NativeEmulation")
                    emulationThread.start()
                }

                State.PAUSED -> {
                    Log.debug("[EmulationFragment] Resuming emulation.")
                    NativeLibrary.unpauseEmulation()
                }

                else -> Log.debug("[EmulationFragment] Bug, run called while already running.")
            }
            state = State.RUNNING
        }

        private enum class State {
            STOPPED, RUNNING, PAUSED
        }
    }

    companion object {
        private val perfStatsUpdateHandler = Handler(Looper.myLooper()!!)
        private val thermalStatsUpdateHandler = Handler(Looper.myLooper()!!)
    }
}
