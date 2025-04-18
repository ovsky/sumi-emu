// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.sumi.sumi_emu.features.settings.ui

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import org.sumi.sumi_emu.SumiApplication
import org.sumi.sumi_emu.adapters.AbstractListAdapter
import org.sumi.sumi_emu.databinding.ListItemInputProfileBinding
import org.sumi.sumi_emu.viewholder.AbstractViewHolder
import org.sumi.sumi_emu.R

class InputProfileAdapter(options: List<ProfileItem>) :
    AbstractListAdapter<ProfileItem, AbstractViewHolder<ProfileItem>>(options) {
    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): AbstractViewHolder<ProfileItem> {
        ListItemInputProfileBinding.inflate(LayoutInflater.from(parent.context), parent, false)
            .also { return InputProfileViewHolder(it) }
    }

    inner class InputProfileViewHolder(val binding: ListItemInputProfileBinding) :
        AbstractViewHolder<ProfileItem>(binding) {
        override fun bind(model: ProfileItem) {
            when (model) {
                is ExistingProfileItem -> {
                    binding.title.text = model.name
                    binding.buttonNew.visibility = View.GONE
                    binding.buttonDelete.visibility = View.VISIBLE
                    binding.buttonDelete.setOnClickListener { model.deleteProfile.invoke() }
                    binding.buttonSave.visibility = View.VISIBLE
                    binding.buttonSave.setOnClickListener { model.saveProfile.invoke() }
                    binding.buttonLoad.visibility = View.VISIBLE
                    binding.buttonLoad.setOnClickListener { model.loadProfile.invoke() }
                }

                is NewProfileItem -> {
                    binding.title.text = model.name
                    binding.buttonNew.visibility = View.VISIBLE
                    binding.buttonNew.setOnClickListener { model.createNewProfile.invoke() }
                    binding.buttonSave.visibility = View.GONE
                    binding.buttonDelete.visibility = View.GONE
                    binding.buttonLoad.visibility = View.GONE
                }
            }
        }
    }
}

sealed interface ProfileItem {
    val name: String
}

data class NewProfileItem(
    val createNewProfile: () -> Unit
) : ProfileItem {
    override val name: String = SumiApplication.appContext.getString(R.string.create_new_profile)
}

data class ExistingProfileItem(
    override val name: String,
    val deleteProfile: () -> Unit,
    val saveProfile: () -> Unit,
    val loadProfile: () -> Unit
) : ProfileItem
