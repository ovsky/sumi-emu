// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.sumi.sumi_emu.adapters

import android.net.Uri
import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.navigation.findNavController
import androidx.preference.PreferenceManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.sumi.sumi_emu.HomeNavigationDirections
import org.sumi.sumi_emu.R
import org.sumi.sumi_emu.SumiApplication
import org.sumi.sumi_emu.databinding.CardGameBinding
import org.sumi.sumi_emu.model.Game
import org.sumi.sumi_emu.model.GamesViewModel
import org.sumi.sumi_emu.utils.GameIconUtils
import org.sumi.sumi_emu.utils.ViewUtils.marquee
import org.sumi.sumi_emu.viewholder.AbstractViewHolder

class GameAdapter(private val activity: AppCompatActivity) :
    AbstractDiffAdapter<Game, GameAdapter.GameViewHolder>(exact = false) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameViewHolder {
        CardGameBinding.inflate(LayoutInflater.from(parent.context), parent, false)
            .also { return GameViewHolder(it) }
    }

    inner class GameViewHolder(val binding: CardGameBinding) :
        AbstractViewHolder<Game>(binding) {
        override fun bind(model: Game) {
            binding.imageGameScreen.scaleType = ImageView.ScaleType.CENTER_CROP
            GameIconUtils.loadGameIcon(model, binding.imageGameScreen)

            binding.textGameTitle.text = model.title.replace("[\\t\\n\\r]+".toRegex(), " ")

            binding.textGameTitle.marquee()
            binding.cardGame.setOnClickListener { onClick(model) }
            binding.cardGame.setOnLongClickListener { onLongClick(model) }
        }

        fun onClick(game: Game) {
            val gameExists = DocumentFile.fromSingleUri(
                SumiApplication.appContext,
                Uri.parse(game.path)
            )?.exists() == true
            if (!gameExists) {
                Toast.makeText(
                    SumiApplication.appContext,
                    R.string.loader_error_file_not_found,
                    Toast.LENGTH_LONG
                ).show()

                ViewModelProvider(activity)[GamesViewModel::class.java].reloadGames(true)
                return
            }

            val preferences =
                PreferenceManager.getDefaultSharedPreferences(SumiApplication.appContext)
            preferences.edit()
                .putLong(
                    game.keyLastPlayedTime,
                    System.currentTimeMillis()
                )
                .apply()

            activity.lifecycleScope.launch {
                withContext(Dispatchers.IO) {
                    val shortcut =
                        ShortcutInfoCompat.Builder(SumiApplication.appContext, game.path)
                            .setShortLabel(game.title)
                            .setIcon(GameIconUtils.getShortcutIcon(activity, game))
                            .setIntent(game.launchIntent)
                            .build()
                    ShortcutManagerCompat.pushDynamicShortcut(SumiApplication.appContext, shortcut)
                }
            }

            val action = HomeNavigationDirections.actionGlobalEmulationActivity(game, true)
            binding.root.findNavController().navigate(action)
        }

        fun onLongClick(game: Game): Boolean {
            val action = HomeNavigationDirections.actionGlobalPerGamePropertiesFragment(game)
            binding.root.findNavController().navigate(action)
            return true
        }
    }
}
